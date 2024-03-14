#!/bin/bash
echo "$@"
pid1=`ps aux | grep $1 | grep -v grep | awk '{print $2}'|head -n 1`
#  echo $pid > /sys/fs/cgroup/memory/my_cgroup/cgroup.procs
#  echo $(($first_arg * 1024 * 1024 * 1024)) > /sys/fs/cgroup/memory/my_cgroup/memory.limit_in_bytes
pcm-power 1 -m -1 &> $1.energy.$2.out &
pid2=$!
date >> $1.ps.$2.out
while true; do
    line=$(ps auxh -q $pid1)
    if [ "$line" == "" ]; then
        break
    fi
    echo $line >> $1.ps.$2.out
    sleep 0.01
    if ! ps -p $pid1 >/dev/null; then
        sleep 0.5
        sudo kill -9 $pid2
        exit 0
    fi
done