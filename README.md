# PMEM

A dirt simple malloc replacement for memory profiling


# Raison d'Etre (Why?)

I needed to track down the source of a memory build up in a rust server
(shocking!) running in production but only when rubbing my stomach while hoping
on one foot with a coconut on my head. The following tools were attempted and
found lacking:

- `valgrind` is too slow which tends to bias the output my scenario.
- `tcmalloc` is in C++ and the ABI makes that a nightmare for production debugging.
- `jemalloc` just didn't output anything. I think it hates me.

That and I did it because I was bored... Mostly because I was bored...


# Building

Configuration is done via the `config.h` file in the root of the repo.

For building, no external dependency is required beyond what's provided
out-of-box in most linux distro:

```
$ mkdir build && build
$ PREFIX=.. ../compile.sh
```

This will produce a `libpmem.so` in the build folder. No install targets are
provided as I don't expect anybody but me will ever use this.


# Using

`pmem` works by replacing malloc which is usually done via `ld`:

```
$ LD_PRELOAD=/path/to/libpmem.so /your/leaky/program/here
```

This will produce a `pmem.$pid.log` file in the current working directory which
will contains a log of the profiler output that looks something like this:

```
[ 62]=========================================================
churn=1049211/1048576

{ca9f786f088a4a78} live:5436, alloc:5/12000, free:6564/6564
  {0} ./libpmem.so(malloc+0x1c) [0x7fd4fca1d89c]
  {1} ./test_basics(+0x11b0) [0x556298df01b0]
  {2} /usr/lib/libc.so.6(__libc_start_main+0xf3) [0x7fd4fc856223]
  {3} ./test_basics(+0x136e) [0x556298df036e]
```

`pmem` works by reccording allocations and deallocations made for each
allocation source which is identified by a stack backtrace. It then periodically
dumps a snapshot of its state in the log file which roughly templates to:

```
[$(snapshot)]===========================================
churn=$(churn_curr)/$(churn_thresh)

{$(source)} live:$(live), alloc:$(alloc_curr)/$(alloc_total), free:($free_curr)/$(free_total)
  $(backtrace)
```

The header consists of the following fields:
- `snapshot`: sequentially incrementing number of the current snapshot
- `churn_curr`: how many bytes were allocated and freed in the snapshot
- `churn_thresh`: how many bytes of churn required to trigger a snapshot

The header is followed by an entry for each allocation source:
- `source`: hash of the backtrace which provides a unique-ish id of the source
- `live`: number of bytes currently allocated but not freed
- `alloc_curr`: number of bytes allocated in this snapshot
- `alloc_total`: number of bytes allocated since start
- `free_curr`: number of bytes freed in this snapshot
- `free_total`: number of bytes freed since start
- `backtrace`: symbolic dump of the source stack trace

Dumping frequency can be tweaked in the `config.h` via the `PMEM_CHURN_THRESH`
option.

Recommended best practice is to pray to the god of debuging symbols, K'alrog The
Vile, for good fortune. Otherwise you'll end up having to hunt addresses using
`objdump` which is not pleasant.


# Can pmem do...?

Nop but it probably could. Besides the re-entrency issues in `prof.c` the code
is simple enough (famous last words) so shouldn't be too hard to adapt to your
use cases.
