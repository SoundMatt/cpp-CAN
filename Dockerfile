# Multi-stage build for cpp-CAN.
# builder stage compiles the library and tests.
# test image exposes the test binary for CI smoke runs.

FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        cmake ninja-build g++-12 ca-certificates git && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER=g++-12 \
        -DCMAKE_CXX_STANDARD=17 && \
    cmake --build build --parallel

# ── test image ─────────────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS test
RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends libstdc++6 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/tests/cppcan_tests /usr/local/bin/cppcan_tests
LABEL org.opencontainers.image.title="cpp-CAN tests" \
      org.opencontainers.image.source="https://github.com/SoundMatt/cpp-CAN" \
      org.opencontainers.image.licenses="MPL-2.0"
ENTRYPOINT ["cppcan_tests"]
CMD ["--reporter", "compact"]
