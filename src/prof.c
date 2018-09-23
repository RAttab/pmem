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

static struct htable live = {0};
static struct htable sources = {0};


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static_assert(sizeof(uint64_t) == sizeof(void *), "portatibility issue");

inline void * pun_itop(uint64_t value)
{
    return (union { uint64_t i; void *p; }) { .i = value }.p;
}

inline uint64_t pun_ptoi(void * value)
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
    static size_t churn = 0;
    static const size_t churn_thresh = 1UL << 24; // 1Mb
    if ((churn += len) < churn_thresh) return;

    char file[256] = {0};
    snprintf(file, sizeof(file), "pmem.%d.log", getpid());
    int fd = open(file, O_CREAT | O_APPEND, 0600);
    assert(fd >= 0);

    static size_t snapshot = 0;
    dprintf(fd,
            "\n[%3zu]=========================================================",
            snapshot++);

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

        backtrace_symbols_fd(source->bt, source->len, fd);
    }
}

void prof_alloc(void *ptr, size_t len)
{
    struct source *source = source_get();
    source->alloc.total++;

    struct htable_ret ret = htable_put(&live, pun_ptoi(ptr), pun_ptoi(source));
    assert(ret.ok);

    prof_dump(len);
}

void prof_free(void *ptr)
{
    struct htable_ret ret = htable_del(&live, pun_ptoi(ptr));
    assert(ret.ok);

    struct source *source = pun_itop(ret.value);
    source->free.total++;
}
