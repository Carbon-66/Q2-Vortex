#!/bin/sh
# host build of the whole game (x86-64 Linux), for the tests: alsa_fake.c stands in for alsa-lib
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
clang -O2 -g $HOSTDEFS -fno-math-errno -Wall -Wextra -Wno-unused-parameter -Werror -pthread src/*.c test/alsa_fake.c -o build/host/q2-vortex
clang -shared -fPIC -O1 -w test/fbshim.c -o build/host/fbshim.so -ldl
