#!/bin/bash

# Make 1000 (possibly) concurrent requests and count number rejected
#
# $ ./test.sh 2>&1 | grep Error -c

redis-cli set data something

for index in {1..1000}
do
    redis-cli get data &
    redis-cli ping &
    redis-cli echo something_very_very_very_long &
done
