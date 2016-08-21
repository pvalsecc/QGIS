#!/bin/bash
#Small wrapper for rocker that downloads it if missing

DIR=`dirname $0`
DOCKER_URL=https://github.com/grammarly/rocker/releases/download/1.3.0/rocker_linux_amd64.tar.gz

if [ ! -f $DIR/tmp/rocker ]
then
    curl -L $DOCKER_URL | tar -x -z -C $DIR/tmp -f -
fi

$DIR/tmp/rocker "$*"
