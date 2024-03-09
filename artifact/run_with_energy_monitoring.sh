#!/bin/bash
echo "$@"
"$@" &> $1.out &
pid1=$!
#  echo $pid > /sys/fs/cgroup/memory/my_cgroup/cgroup.procs
#  echo $(($first_arg * 1024 * 1024 * 1024)) > /sys/fs/cgroup/memory/my_cgroup/memory.limit_in_bytes
while true; do
    line=$(ps auxh -q $pid1)
    if [ "$line" == "" ]; then
        break
    fi
    echo $line >>$1.energy.out
    for child in $(pgrep -P $pid1); do
        line=$(ps auxh -q $child)
        if [ "$line" == "" ]; then
            continue
        fi
        echo $line >>$1.energy.out
    done
    sleep 0.005
    if ! ps -p $pid >/dev/null; then
        sleep 0.5
    fi
done