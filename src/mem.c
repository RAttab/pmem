#include <sys/mman.h>

#include "common.h"


// -----------------------------------------------------------------------------
// state
// -----------------------------------------------------------------------------

static lock_t mem_lock = 0;
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


// -----------------------------------------------------------------------------
// mem
// -----------------------------------------------------------------------------

static void *mem_alloc_impl(size_t len)
{
   size_t bucket = len_to_bucket(len);
   return bucket == bucket_vma ? vma_alloc(len) : bucket_alloc(bucket);
}

static void mem_free_impl(void *ptr)
{
    size_t bucket = ptr_to_bucket(ptr);
    bucket == bucket_vma ? vma_free(ptr) : bucket_free(bucket, ptr);
}

static size_t mem_usable_size_impl(void *ptr)
{
    size_t bucket = ptr_to_bucket(ptr);
    return bucket == bucket_vma ? vma_usable_size(ptr) : bucket_to_len(bucket);
}


void *mem_alloc(size_t len)
{
    pmem_lock(&mem_lock);
    void *ptr = mem_alloc_impl(len);
    pmem_unlock(&mem_lock);
    return ptr;
}

void *mem_calloc(size_t n, size_t len)
{
    void *ptr = mem_alloc(n * len);
    memset(ptr, 0, n * len);
    return ptr;
}

void mem_free(void *ptr)
{
    if (!ptr) return;

    pmem_lock(&mem_lock);
    mem_free_impl(ptr);
    pmem_unlock(&mem_lock);
}


void *mem_realloc(void *ptr, size_t len)
{
    pmem_lock(&mem_lock);

    void *new = mem_alloc_impl(len);
    memcpy(new, ptr, mem_usable_size_impl(ptr));
    mem_free_impl(ptr);

    pmem_unlock(&mem_lock);
    return new;
}

size_t mem_usable_size(void *ptr)
{
    pmem_lock(&mem_lock);
    size_t len = mem_usable_size_impl(ptr);
    pmem_unlock(&mem_lock);
    return len;
}
