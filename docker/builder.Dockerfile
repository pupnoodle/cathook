# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /tmp/PUPHOOK-bootstrap

COPY install-deps ./install-deps
COPY packages ./packages

RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get update \
 && apt-get install -y --no-install-recommends sudo git wget ca-certificates \
 && printf '#!/bin/sh\nexec "$@"\n' > /usr/local/bin/sudo
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    chmod +x /usr/local/bin/sudo \
 && bash ./install-deps \
 && rm -rf /tmp/PUPHOOK-bootstrap

WORKDIR /workspace
