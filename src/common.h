#pragma once

#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// -----------------------------------------------------------------------------
// attributes
// -----------------------------------------------------------------------------

#define pmem_public __attribute__((visibility("default")))
#define pmem_malloc __attribute__((malloc))


// -----------------------------------------------------------------------------
// mem
// -----------------------------------------------------------------------------

pmem_malloc void *mem_alloc(size_t len);
pmem_malloc void *mem_calloc(size_t n, size_t len);
void *mem_realloc(void *ptr, size_t len);
void mem_free(void *ptr);


// -----------------------------------------------------------------------------
// prof
// -----------------------------------------------------------------------------

void prof_alloc(void *ptr, size_t len);
void prof_free(void *ptr);


// -----------------------------------------------------------------------------
// htable
// -----------------------------------------------------------------------------

struct htable_bucket
{
    uint64_t key;
    uint64_t value;
};

struct htable
{
    size_t len;
    size_t cap;
    struct htable_bucket *table;
};

struct htable_ret
{
    bool ok;
    uint64_t value;
};


void htable_reset(struct htable *);
void htable_reserve(struct htable *, size_t items);
struct htable_ret htable_get(struct htable *, uint64_t key);
struct htable_ret htable_put(struct htable *, uint64_t key, uint64_t value);
struct htable_ret htable_xchg(struct htable *, uint64_t key, uint64_t value);
struct htable_ret htable_del(struct htable *, uint64_t key);
struct htable_bucket * htable_next(struct htable *, struct htable_bucket *bucket);
