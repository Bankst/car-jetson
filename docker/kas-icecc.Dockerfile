FROM ghcr.io/siemens/kas/kas:4.7

USER root
RUN apt-get update \
    && apt-get install -y --no-install-recommends icecc \
    && rm -rf /var/lib/apt/lists/*
USER builder
