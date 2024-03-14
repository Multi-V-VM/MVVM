#!/bin/bash
echo "$@"
"$@" &> $1.out &
pid1=$!
echo $pid1
#  echo $pid > /sys/fs/cgroup/memory/my_cgroup/cgroup.procs
#  echo $(($first_arg * 1024 * 1024 * 1024)) > /sys/fs/cgroup/memory/my_cgroup/memory.limit_in_bytes
date >> $1.ps.out
while true; do
    line=$(ps auxh -q $pid1)
    if [ "$line" == "" ]; then
        break
    fi
    echo $line >>$1.ps.out
    sleep 0.5
    if ! ps -p $pid1 >/dev/null; then
        sleep 0.5
        exit 0
    fi
done