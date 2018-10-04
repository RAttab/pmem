#include <sys/mman.h>

#include "common.h"


// -----------------------------------------------------------------------------
// state
// -----------------------------------------------------------------------------

static lock_t lock = 0;
static void *buckets[8] = {0};
static const size_t bucket_vma = -1UL;
static const size_t page_len = 4096UL;


// -----------------------------------------------------------------------------
// utils
// -----------------------------------------------------------------------------

static inline uint64_t ptr_read_u64(void *ptr)
{
    return *((uint64_t *) ptr);
}

static inline void ptr_write_u64(void *ptr, uint64_t data)
{
    *((uint64_t *) ptr) = data;
}

static inline void *ptr_inc(void *ptr, size_t len)
{
    return ((uint8_t *) ptr) + len;
}

static inline void *ptr_dec(void *ptr, size_t len)
{
    return ((uint8_t *) ptr) - len;
}

static size_t ptr_to_bucket(void *ptr)
{
    static const uintptr_t mask = page_len - 1;
    void *masked = (void *) (((uint64_t) ptr) & ~mask);
    return ptr == masked ? bucket_vma : ptr_read_u64(masked);
}

static size_t len_to_bucket(size_t len)
{
    if (len <= 8) return 0;
    if (len <= 16) return 1;
    if (len <= 32) return 2;
    if (len <= 64) return 3;
    if (len <= 128) return 4;
    if (len <= 256) return 5;
    if (len <= 512) return 6;
    if (len <= 1024) return 7;
    return bucket_vma;
}

static size_t bucket_to_len(size_t bucket)
{
    return 1UL << (4 + bucket);
}

// -----------------------------------------------------------------------------
// vma
// -----------------------------------------------------------------------------

static inline size_t to_vma_len(size_t len)
{
    return (len + (page_len - 1)) & ~(page_len - 1);
}

static void *vma_alloc(size_t len)
{
    size_t vma_len = to_vma_len(len) + page_len;
    void *ptr = mmap(0, vma_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (!ptr) return NULL;

    ptr_write_u64(ptr, vma_len);
    return ptr_inc(ptr, page_len);
}

static void vma_free(void *raw)
{
    void *ptr = ptr_dec(raw, page_len);
    size_t vma_len = ptr_read_u64(ptr);

    munmap(ptr, vma_len);
}

static void *vma_realloc(void *raw, size_t len)
{
    void *ptr = ptr_dec(raw, page_len);
    size_t vma_len = ptr_read_u64(ptr);

    if ((vma_len - page_len) >= len) return raw;

    size_t new_vma_len = to_vma_len(len) + page_len;
    void *new_ptr = mremap(ptr, vma_len, new_vma_len, MREMAP_MAYMOVE);
    if (!new_ptr) return NULL;

    ptr_write_u64(new_ptr, new_vma_len);
    return ptr_inc(new_ptr, page_len);
}

static size_t vma_usable_size(void *raw)
{
    void *ptr = ptr_dec(raw, page_len);
    return ptr_read_u64(ptr) - page_len;
}


// -----------------------------------------------------------------------------
// bucket
// -----------------------------------------------------------------------------

static void *bucket_alloc(size_t bucket)
{
    size_t len = bucket_to_len(bucket);

    if (!buckets[bucket]) {
        void *ptr = mmap(0, page_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if (!ptr) return NULL;

        ptr_write_u64(ptr, bucket);

        void *first = ptr_inc(ptr, len);
        void *last = ptr_inc(ptr, page_len - len);
        for (void *it = first; it < last; it = ptr_inc(it, len))
            ptr_write_u64(it, (uint64_t) ptr_inc(it, len));

        buckets[bucket] = first;
    }

    void *ptr = buckets[bucket];
    buckets[bucket] = (void *) ptr_read_u64(ptr);
    return ptr;
}

static void bucket_free(size_t bucket, void *ptr)
{
    ptr_write_u64(ptr, (uint64_t) buckets[bucket]);
    buckets[bucket] = ptr;
}

static void *bucket_realloc(size_t bucket, void *ptr, size_t new_len)
{
    size_t len = bucket_to_len(bucket);
    if (len >= new_len) return ptr;

    void *new_ptr = mem_alloc(new_len);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, len);
    bucket_free(bucket, ptr);

    return new_ptr;
}


// -----------------------------------------------------------------------------
// mem
// -----------------------------------------------------------------------------

void *mem_alloc(size_t len)
{
    pmem_lock(&lock);

    size_t bucket = len_to_bucket(len);
    void *ptr = bucket == bucket_vma ? vma_alloc(len) : bucket_alloc(bucket);

    pmem_unlock(&lock);
    return ptr;
}

void *mem_calloc(size_t n, size_t len)
{
    pmem_lock(&lock);

    void *ptr = mem_alloc(n * len);
    memset(ptr, 0, n * len);

    pmem_unlock(&lock);
    return ptr;
}

void mem_free(void *ptr)
{
    pmem_lock(&lock);

    size_t bucket = ptr_to_bucket(ptr);
    bucket == bucket_vma ? vma_free(ptr) : bucket_free(bucket, ptr);

    pmem_unlock(&lock);
}

void *mem_realloc(void *ptr, size_t len)
{
    pmem_lock(&lock);

    size_t bucket = ptr_to_bucket(ptr);
    void *new = bucket == bucket_vma ?
        vma_realloc(ptr, len) : bucket_realloc(bucket, ptr, len);

    pmem_unlock(&lock);
    return new;
}

size_t mem_usable_size(void *ptr)
{
    pmem_lock(&lock);

    size_t bucket = ptr_to_bucket(ptr);
    size_t len = bucket == bucket_vma ?
        vma_usable_size(ptr) : bucket_to_len(bucket);

    pmem_unlock(&lock);
    return len;
}
