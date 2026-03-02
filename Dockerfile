FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    gcc-14 g++-14 \
    ninja-build python3 python3-pip git ca-certificates ccache \
    && rm -rf /var/lib/apt/lists/*

RUN pip install --break-system-packages cmake

ENV CC=gcc-14
ENV CXX=g++-14
