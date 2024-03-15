#!/bin/bash
echo "$@"
pid1=`ps aux | grep $1 | grep -v grep |grep -v mitoring |grep -v $3 | awk '{print $2}'|head -n 1`
sudo asitop --show_cores 1 &> $1.energy.$2.out &
pid2=$!
date >> /Users/victoryang00/Documents/project/MVVM-bench/$1.ps.$2.out
while true; do
    line=$(ps -p $pid1 -o user,pid,%cpu,%mem,vsz,rss,tt,stat,start,time,command)
    if [ "$line" == "" ]; then
        break
    fi
    echo $line >> $1.ps.$2.out
    sleep 0.5
    if ! ps -p $pid1 >/dev/null; then
        sleep 0.5
        sudo kill -9 $pid2
        exit 0
    fi
done