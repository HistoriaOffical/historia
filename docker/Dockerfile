FROM debian:stretch
LABEL maintainer="Historia Developers <dev@historia.org>"
LABEL description="Dockerised HistoriaCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
