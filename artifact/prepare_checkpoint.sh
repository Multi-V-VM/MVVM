#!/bin/bash

ssh root@192.168.122.147 "$@" &

{ echo "migrate -d tcp:localhost:4444"; sleep 1; } | telnet localhost 1234
sleep 5
{ echo "info migrate"; sleep 1; } | telnet localhost 1234
