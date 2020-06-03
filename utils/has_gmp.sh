#!/bin/sh

# GMP support checking
# Copyright (c) 2019, Christopher Jeffrey (MIT License).
# https://github.com/bcoin-org/bcrypto
#
# Tested with shells: bash, dash, busybox
# Tested with compilers: gcc, clang
#
# We try to compile some code specifically
# written to fail if the compiler is linking
# to mini-gmp instead of gmp.

echo 'false';

exit 0
