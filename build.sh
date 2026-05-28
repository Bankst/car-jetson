#!/bin/bash
# Wrapper around kas-container. Defaults to icecc (mandatory: local.conf
# has INHERIT += "icecc" and kernel do_configure fails without iceccd).
# Pass --no-icecc to force plain build (only useful if iceccd is down
# and you've temporarily commented INHERIT in local.conf).
set -euo pipefail

USE_ICECC=1
VARIANT=""
for arg in "$@"; do
    case "$arg" in
        --no-icecc) USE_ICECC=0 ;;
        *)          VARIANT="$arg" ;;
    esac
done
VARIANT="${VARIANT:-base}"

export KAS_BUILD_DIR="$PWD/build"

if [[ "$USE_ICECC" -eq 1 ]]; then
    export KAS_CONTAINER_IMAGE="kas-icecc:4.7"
    exec kas-container \
        --runtime-args "--security-opt label=disable" \
        --runtime-args "--network=host" \
        --runtime-args "-v /run/icecc:/var/run/icecc:rw" \
        build "kas/${VARIANT}.yml"
else
    exec kas-container \
        --runtime-args "--security-opt label=disable" \
        build "kas/${VARIANT}.yml"
fi
