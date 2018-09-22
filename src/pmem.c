#include "common.h"


// -----------------------------------------------------------------------------
// public
// -----------------------------------------------------------------------------

void *malloc(size_t size)
{
    void *ptr = mem_alloc(size);
    return ptr;
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr = mem_calloc(nmemb, size);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    void *new_ptr = mem_realloc(ptr, size);
    return new_ptr;
}

void free(void *ptr)
{
    mem_free(ptr);
}
