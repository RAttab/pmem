#! /usr/bin/env bash

set -o errexit -o nounset -o pipefail -o xtrace

: ${PREFIX:="."}

declare -a SRC
SRC=(htable mem prof pmem)

declare -a TEST
TEST=(basics)

CC=${OTHERC:-gcc}

CFLAGS="-ggdb -O3 -march=native -pipe -std=gnu11 -D_GNU_SOURCE"
CFLAGS="$CFLAGS -I${PREFIX}/src"

CFLAGS="$CFLAGS -fPIC"
CFLAGS="$CFLAGS -fvisibility=hidden"
CFLAGS="$CFLAGS -fno-strict-aliasing"

CFLAGS="$CFLAGS -Werror -Wall -Wextra"
CFLAGS="$CFLAGS -Wundef"
CFLAGS="$CFLAGS -Wcast-align"
CFLAGS="$CFLAGS -Wwrite-strings"
CFLAGS="$CFLAGS -Wunreachable-code"
CFLAGS="$CFLAGS -Wformat=2"
CFLAGS="$CFLAGS -Wswitch-enum"
CFLAGS="$CFLAGS -Wswitch-default"
CFLAGS="$CFLAGS -Winit-self"
CFLAGS="$CFLAGS -Wno-strict-aliasing"
CFLAGS="$CFLAGS -Wno-implicit-fallthrough"

# Feels a bit dirty but oh well
grep -q '^#define PMEM_LIBUNWIND$' "${PREFIX}/config.h" && \
    CFLAGS="$CFLAGS -lunwind"

OBJ=""
for src in "${SRC[@]}"; do
    $CC -c -o "$src.o" "${PREFIX}/src/$src.c" $CFLAGS
    OBJ="$OBJ $src.o"
done

$CC -o libpmem.so -shared $OBJ

for test in "${TEST[@]}"; do
    $CC -o "test_$test" "${PREFIX}/test/$test.c" $CFLAGS
    LD_PRELOAD=./libpmem.so "./test_$test"
done
