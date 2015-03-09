#!/bin/bash

NUM=1
for t in ./group_test_*.sh; do
    if [ ! -x "$t" ]; then
        continue
    fi
    echo "$NUM) Running test: $t"
    "$t"
    ((NUM++))
done
