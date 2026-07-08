# Nebula Io 계층(Linux: io_uring / epoll) 빌드·테스트용 환경.
#
# 사용법:
#   docker build -t nebula-io -f Dockerfile .
#   docker run --rm nebula-io                     # NebulaIoTest 실행
#
# 주의:
#   - C++23 을 위해 gcc-14 사용(Ubuntu 24.04).
#   - io_uring 런타임 테스트는 호스트 커널 지원(>=5.1)에 의존한다. Docker 기본 seccomp 는
#     io_uring 시스템콜을 차단하므로 IoUringEngine 테스트는 SKIP 되고 EpollEngine 폴백만 검증된다.
#     io_uring 까지 검증하려면:  docker run --rm --security-opt seccomp=unconfined nebula-io
#   - 엔진 인터페이스 in-place 개조 중이라 상위 레이어(Network Stream 등)는 스코프 밖 —
#     기본 빌드 타깃은 NebulaIoTest 로 한정한다.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        g++-14 \
        cmake \
        ninja-build \
        git \
        pkg-config \
        liburing-dev \
        libssl-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-14
ENV CXX=g++-14

WORKDIR /src
COPY . .

# 구성은 전체 프로젝트를 대상으로 하되(FetchContent: googletest), 빌드는 Io 계층 타깃만.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    && cmake --build build --target NebulaIoTest

CMD ["./build/Io/Test/NebulaIoTest"]
