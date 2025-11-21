#!/bin/bash

# Make 1000 (possibly) concurrent requests and count number rejected
#
# $ ./test.sh 2>&1 | grep Error | wc -l

redis-cli set data something

for Variable in {1..500}
do
    redis-cli get data &
done

for Variable in {1..500}
do
    redis-cli ping &
done
