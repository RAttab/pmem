#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "common.h"


// -----------------------------------------------------------------------------
// lock
// -----------------------------------------------------------------------------

// linux is fucking stupid sometimes...
static inline int futex(atomic_int *uaddr, int futex_op, int val)
{
    return syscall(SYS_futex, (int *) uaddr, futex_op, val, NULL, 0, 0);
}

void pmem_lock(lock_t *lock)
{
    int exp = 0;
    while (!atomic_compare_exchange_strong(lock, &exp, 1)) {
        futex(lock, FUTEX_WAIT, 1);
        exp = 0;
    }
}

bool pmem_try_lock(lock_t *lock)
{
    int exp = 0;
    return atomic_compare_exchange_strong(lock, &exp, 1);
}

void pmem_unlock(lock_t *lock)
{
    atomic_store(lock, 0);
    futex(lock, FUTEX_WAKE, 1);
}


// -----------------------------------------------------------------------------
// basic
// -----------------------------------------------------------------------------

pmem_public void *malloc(size_t size)
{
    void *ptr = mem_alloc(size);
    prof_alloc(ptr, size);
    return ptr;
}

pmem_public void *calloc(size_t nmemb, size_t size)
{
    void *ptr = mem_calloc(nmemb, size);
    prof_alloc(ptr, nmemb * size);
    return ptr;
}

pmem_public void *realloc(void *old, size_t size)
{
    prof_free(old);
    void *new = mem_realloc(old, size);
    prof_alloc(new, size);
    return new;
}

pmem_public void free(void *ptr)
{
    prof_free(ptr);
    mem_free(ptr);
}


// -----------------------------------------------------------------------------
// extended
// -----------------------------------------------------------------------------

static inline size_t align(size_t align, size_t size)
{
    return (size + (align - 1)) & ~(align - 1);
}

pmem_public int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    *memptr = malloc(align(alignment, size));
    return *memptr ? 0 : ENOMEM;
}

pmem_public void *aligned_alloc(size_t alignment, size_t size)
{
    return malloc(align(alignment, size));
}

pmem_public void *memalign(size_t alignment, size_t size)
{
    return malloc(align(alignment, size));
}

pmem_public void *valloc(size_t size)
{
    return memalign(sysconf(_SC_PAGE_SIZE), size);
}

pmem_public void *pvalloc(size_t size)
{
    return malloc(align(size, sysconf(_SC_PAGE_SIZE)));
}

pmem_public size_t malloc_usable_size(void *ptr)
{
    return mem_usable_size(ptr);
}
