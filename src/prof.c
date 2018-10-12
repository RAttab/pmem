#include "common.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

// -----------------------------------------------------------------------------
// state
// -----------------------------------------------------------------------------


struct frame
{
    char name[128];
    uint64_t off;
};

struct source
{
    uint64_t hash;
    struct { size_t total, prev; } alloc, free;

    size_t len;
    struct frame bt[];
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

static inline uint64_t addr_hash(uint64_t hash, uint64_t addr)
{
    const uint8_t *data = (uint8_t *) &addr;
    if (!hash) hash = 0xcbf29ce484222325;

    for (size_t i = 0; i < sizeof(addr); ++i)
        hash = (hash ^ data[i]) * 0x100000001b3;

    return hash;
}

#ifdef PMEM_LIBUNWIND

#define UNW_LOCAL_ONLY
#include <libunwind.h>

static const char frame_unknown[] = "unknown";
static_assert(sizeof(frame_unknown) < sizeof((struct frame){}.name), "unknown symbol too big");

static void source_hash(uint64_t *hash, size_t *len)
{
    unw_context_t ctx;
    unw_getcontext(&ctx);

    unw_cursor_t cursor;
    unw_init_local(&cursor, &ctx);

    *len = 0;
    *hash = 0;

    while (unw_step(&cursor) > 0) {
        unw_word_t ip;
        unw_get_reg(&cursor, UNW_REG_IP, &ip);
        *hash = addr_hash(*hash, ip);
        (*len)++;
    }
}

static void source_bt(struct source *source)
{
    unw_context_t ctx;
    unw_getcontext(&ctx);

    unw_cursor_t cursor;
    unw_init_local(&cursor, &ctx);

    for (size_t i = 0; unw_step(&cursor) > 0; ++i) {
        struct frame *frame = &source->bt[i];

        int ret = unw_get_proc_name(&cursor, frame->name, sizeof(frame->name), &frame->off);
        if (ret == -UNW_ENOINFO) memcpy(frame->name, frame_unknown, sizeof(frame_unknown));
        else if (ret != -UNW_ENOMEM) assert(!ret);
    }
}

#else

#include <execinfo.h>

static void source_hash(uint64_t *hash, size_t *len)
{
    void *bt[256];
    *len = backtrace(bt, sizeof(bt) / sizeof(bt[0]));

    for (size_t i = 0; i < *len; ++i) {
        *hash = addr_hash(*hash, (uint64_t) bt[i]);
    }
}

static void source_bt(struct source *source)
{
    void *bt[256];
    size_t len = backtrace(bt, sizeof(bt) / sizeof(bt[0]));
    assert(len == source->len);

    char **symbols = backtrace_symbols(bt, source->len);

    for (size_t i = 0; i < len; ++i) {
        struct frame *frame = &source->bt[i];
        size_t len = strnlen(symbols[i], sizeof(frame->name));
        memcpy(frame->name, symbols[i], len);
    }

    free(symbols);
}

#endif

static struct source *source_get()
{
    uint64_t hash; size_t len;
    source_hash(&hash, &len);

    struct htable_ret ret = htable_get(&sources, hash);
    if (ret.ok) return pun_itop(ret.value);

    struct source *source = mem_calloc(1, sizeof(*source) + sizeof(source->bt[0]) * len);
    source->hash = hash;
    source->len = len;
    source_bt(source);

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

        size_t live = source->alloc.total - source->free.total;
        size_t allocated = source->alloc.total - source->alloc.prev;
        size_t freed = source->free.total - source->free.prev;
        if (!live || (!allocated && !freed)) continue;

        dprintf(fd, "\n{%lx} live:%zu, alloc:%zu/%zu, free:%zu/%zu\n",
                source->hash, live,
                allocated, source->alloc.total,
                freed, source->free.total);
        source->alloc.prev = source->alloc.total;
        source->free.prev = source->free.total;

        for (size_t i = 0; i < source->len; ++i) {
            struct frame *frame = &source->bt[i];
            if (!frame->off)
                dprintf(fd, "  {%zu} %s\n", i, frame->name);
            else
                dprintf(fd, "  {%zu} %s+%lu\n", i, frame->name, frame->off);
        }
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
