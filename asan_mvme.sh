#!/usr/bin/env bash

export ASAN_SYMBOLIZER=`which llvm-symbolizer`
export ASAN_OPTIONS="symbolize=1"

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

export LSAN_OPTIONS="suppressions=${DIR}/asan_mvme.supp"
