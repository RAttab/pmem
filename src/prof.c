#include "common.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <sys/types.h>


// -----------------------------------------------------------------------------
// state
// -----------------------------------------------------------------------------

struct source
{
    uint64_t hash;
    struct { size_t total, prev; } alloc, free;

    size_t len;
    void *bt[];
};

// Used to protect the prof htables.
static lock_t prof_lock = 0;

// Used to ensure that we only have a single dumper running. This is usually
// used in a non-blocking fashion (ie. if locked, skip dumping).
static lock_t dump_lock = 0;

// This flag is used to ignore profiling allocations made by the profiler which
// also avoids re-entrency issues.
static __thread bool profiling = 0;

// Policy for when to dump.
static atomic_size_t churn = 0;
static const size_t churn_thresh = PMEM_CHURN_THRESH;

static struct htable live = {0};
static struct htable sources = {0};


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static_assert(sizeof(uint64_t) == sizeof(void *), "portatibility issue");

static inline void * pun_itop(uint64_t value)
{
    return (union { uint64_t i; void *p; }) { .i = value }.p;
}

static inline uint64_t pun_ptoi(void * value)
{
    return (union { uint64_t i; void *p; }) { .p = value }.i;
}


// -----------------------------------------------------------------------------
// source
// -----------------------------------------------------------------------------

static inline uint64_t source_hash(void **bt, size_t len)
{
    const uint8_t *data = (uint8_t *) bt;

    uint64_t hash = 0xcbf29ce484222325;
    for (size_t i = 0; i < len * sizeof(*bt); ++i)
        hash = (hash ^ data[i]) * 0x100000001b3;

    assert(hash); // \todo Can't be 0
    return hash;
}


static struct source *source_get()
{
    void *bt[256];
    int len = backtrace(bt, sizeof(bt) / sizeof(bt[0]));
    uint64_t hash = source_hash(bt, len);

    struct htable_ret ret = htable_get(&sources, hash);
    if (ret.ok) return pun_itop(ret.value);

    struct source *source = mem_calloc(1, sizeof(*source) + sizeof(source->bt[0]) * len);
    source->hash = hash;
    source->len = len;
    memcpy(source->bt, bt, sizeof(bt[0]) * len);

    ret = htable_put(&sources, hash, pun_ptoi(source));
    assert(ret.ok);

    return source;
}


// -----------------------------------------------------------------------------
// prof
// -----------------------------------------------------------------------------

static void prof_dump(size_t len)
{
    size_t churn_current = atomic_fetch_add(&churn, len) + len;
    if (churn_current < churn_thresh) return;

    if (!pmem_try_lock(&dump_lock)) return;
    pmem_lock(&prof_lock);
    profiling = true;

    atomic_store(&churn, 0);

    char file[256] = {0};
    snprintf(file, sizeof(file), "./pmem.%d.log", getpid());
    int fd = open(file, O_CREAT | O_APPEND | O_WRONLY, 0600);
    if (fd == -1) {
        fprintf(stderr, "unable to open '%s': %s(%d)\n", file, strerror(errno), errno);
        goto fail_dump;
    }

    static size_t snapshot = 0;
    dprintf(fd,
            "\n[%3zu]=========================================================\n"
            "churn=%zu/%zu\n",
            snapshot++, churn_current, churn_thresh);


    for (struct htable_bucket *it = htable_next(&sources, NULL); it;
         it = htable_next(&sources, it))
    {
        struct source *source = pun_itop(it->value);

        dprintf(fd, "\n{%lx} live:%zu, alloc:%zu/%zu, free:%zu/%zu\n",
                source->hash,
                source->alloc.total - source->free.total,
                source->alloc.total - source->alloc.prev,
                source->alloc.total,
                source->free.total - source->free.prev,
                source->free.total);
        source->alloc.prev = source->alloc.total;
        source->free.prev = source->free.total;

        char **bt = backtrace_symbols(source->bt, source->len);
        for (size_t i = 2; i < source->len; ++i)
            dprintf(fd, "  {%zu} %s\n", i - 2, bt[i]);
        free(bt); // don't think too hard about what this does...
    }


    close(fd);
  fail_dump:
    profiling = false;
    pmem_unlock(&prof_lock);
    pmem_unlock(&dump_lock);
}

void prof_alloc(void *ptr, size_t len)
{
    if (profiling) return;
    pmem_lock(&prof_lock);
    profiling = true;

    struct source *source = source_get();
    source->alloc.total++;

    struct htable_ret ret = htable_put(&live, pun_ptoi(ptr), pun_ptoi(source));
    assert(ret.ok);

    profiling = false;
    pmem_unlock(&prof_lock);

    prof_dump(len);
}

void prof_free(void *ptr)
{
    if (profiling) return;
    pmem_lock(&prof_lock);
    profiling = true;

    struct htable_ret ret = htable_del(&live, pun_ptoi(ptr));
    assert(ret.ok);

    struct source *source = pun_itop(ret.value);
    source->free.total++;

    profiling = false;
    pmem_unlock(&prof_lock);

    prof_dump(mem_usable_size(ptr));
}
