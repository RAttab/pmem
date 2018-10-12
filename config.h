
// If defined, pmem will use libunwind to collect the source's stack
// frames. Otherwise, pmem will fallback on glibc's backtrace.
#define PMEM_LIBUNWIND

// Defines the threshold to dump a memory profile in bytes allocated and freed.
#define PMEM_CHURN_THRESH (1UL << 20) // 1Mb
