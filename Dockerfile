FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libsnappy-dev \
    python3 \
    python3-pip \
    vim \
    nano \
    gdb \
    valgrind \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

RUN git clone --recurse-submodules https://github.com/google/leveldb.git

RUN git clone https://github.com/efficient/SuRF.git

RUN mkdir -p /workspace/leveldb/include/surf && \
    cp -r /workspace/SuRF/include/* /workspace/leveldb/include/surf/

RUN sed -i '/"util\/bloom.cc"/a\    "util/surf_filter.cc"' \
    /workspace/leveldb/CMakeLists.txt

RUN cd /workspace/leveldb && \
    mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DLEVELDB_BUILD_TESTS=ON \
          -DLEVELDB_BUILD_BENCHMARKS=ON \
          .. && \
    cmake --build . --parallel 4

RUN /workspace/leveldb/build/leveldb_tests

RUN mkdir -p /workspace/project /workspace/benchmarks

CMD ["/bin/bash"]
