#include <stdatomic.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include "common.h"


// -----------------------------------------------------------------------------
// lock
// -----------------------------------------------------------------------------

static atomic_int lock = 0;

// linux is fucking stupid sometimes...
static int futex(atomic_int *uaddr, int futex_op, int val)
{
    return syscall(SYS_futex, (int *) uaddr, futex_op, val, NULL, 0, 0);
}

static inline void pmem_lock()
{
    int exp = 0;
    while (!atomic_compare_exchange_strong(&lock, &exp, 1)) {
        futex(&lock, FUTEX_WAIT, 1);
        exp = 0;
    }
}

static inline void pmem_unlock()
{
    atomic_store(&lock, 0);
    futex(&lock, FUTEX_WAKE, 1);
}

// -----------------------------------------------------------------------------
// pmem
// -----------------------------------------------------------------------------

#define pmem_public __attribute__((visibility("default")))

pmem_public void *malloc(size_t size)
{
    void *ptr = NULL;
    {
        pmem_lock();

        ptr = mem_alloc(size);
        prof_alloc(ptr, size);

        pmem_unlock();
    }
    return ptr;
}

pmem_public void *calloc(size_t nmemb, size_t size)
{
    void *ptr = NULL;
    {
        pmem_lock();

        ptr = mem_calloc(nmemb, size);
        prof_alloc(ptr, nmemb * size);

        pmem_unlock();
    }
    return ptr;
}

pmem_public void *realloc(void *old, size_t size)
{
    void *new = NULL;
    {
        pmem_lock();

        prof_free(old);
        new = mem_realloc(old, size);
        prof_alloc(new, size);

        pmem_unlock();
    }
    return new;
}

pmem_public void free(void *ptr)
{
    pmem_lock();

    mem_free(ptr);
    prof_free(ptr);

    pmem_unlock();
}
