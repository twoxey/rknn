#!/bin/sh

set -xe

cd $(dirname $0)

ip a
sudo LD_LIBRARY_PATH=./rknpu2/Linux/aarch64 $@ > /dev/null # > log-$(date -Iseconds).txt
