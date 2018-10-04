#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>

#define sizeof_arr(arr) (sizeof(arr) / sizeof(arr[0]))

int main(int argc, char **argv)
{
    (void) argc, (void) argv;

    enum { iterations = 1000 };
    size_t sizes[] = { 1, 7, 8, 9, 13, 16, 511, 512, 513, 1024, 1025, (1UL << 16) - 1 };

    static void *data[sizeof_arr(sizes)][iterations] = {0};

    for (size_t i = 0; i < sizeof_arr(sizes); ++i) {
        for (size_t j = 0; j < iterations; ++j) {
            void *ptr = data[i][j] = malloc(sizes[i]);

            size_t usable = malloc_usable_size(ptr);
            assert(usable >= 8 && usable >= sizes[i]);

            *((size_t *) ptr) = sizes[i];
        }
    }

    for (size_t i = 0; i < sizeof_arr(sizes); ++i) {
        for (size_t j = 0; j < iterations; ++j) {
            void *ptr = data[i][j];
            assert(malloc_usable_size(ptr) >= sizes[i]);

            assert(*((size_t *) ptr) == sizes[i]);
            free(ptr);
        }
    }
}
