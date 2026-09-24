# The performance job's toolchain, for reproducing its numbers locally.
#
#   docker build -t ce-perf bench/perf
#   bench/perf/run_in_docker.sh . results.json
#
# The image runs the same install script as the CI job. CI runs on x86-64, and
# an instruction count depends on the architecture, so budgets are seeded from
# CI (or from this image built with --platform linux/amd64), never from an
# arm64 container.
FROM ubuntu:24.04

COPY install_toolchain.sh /usr/local/bin/ce-install-perf-toolchain
RUN ce-install-perf-toolchain && rm -rf /var/lib/apt/lists/*

ENV CXX=g++-14
WORKDIR /work
