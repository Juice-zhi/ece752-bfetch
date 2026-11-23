# Build and run environment for the CBP2025 simulator
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ make zlib1g-dev python3 python3-pandas xz-utils \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
