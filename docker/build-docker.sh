#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/..

DOCKER_IMAGE=${DOCKER_IMAGE:-historia/historiad-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/historiad docker/bin/
cp $BUILD_DIR/src/historia-cli docker/bin/
cp $BUILD_DIR/src/historia-tx docker/bin/
strip docker/bin/historiad
strip docker/bin/historia-cli
strip docker/bin/historia-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
