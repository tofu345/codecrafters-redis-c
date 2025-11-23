#!/bin/bash

# Make 1000 (possibly) concurrent requests and count number rejected
#
# $ ./test.sh 2>&1 | grep Error -c

redis-cli set data something

for index in {1..500}
do
    redis-cli get data &
    redis-cli ping &
done
