#!/bin/bash
echo "$@"
pid1=`ps aux | grep $1 | grep -v grep | awk '{print $2}'|tail -n 1`
#  echo $pid > /sys/fs/cgroup/memory/my_cgroup/cgroup.procs
#  echo $(($first_arg * 1024 * 1024 * 1024)) > /sys/fs/cgroup/memory/my_cgroup/memory.limit_in_bytes
pcm-power 1 -m -1 &> $1.$2.energy.out &
pid2=$!
while true; do
    line=$(ps auxh -q $pid1)
    if [ "$line" == "" ]; then
        break
    fi
    date >> $1.$2.ps.out
    echo $line >> $1.$2.ps.out
    for child in $(pgrep -P $pid1); do
        line=$(ps auxh -q $child)
        if [ "$line" == "" ]; then
            continue
        fi
        date >> $1.$2.ps.out
        echo $line >> $1.$2.ps.out
    done
    sleep 0.01
    if ! ps -p $pid1 >/dev/null; then
        sleep 0.5
        sudo kill -9 $pid2
    fi
done