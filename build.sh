#!/bin/bash
set -euo pipefail
KAS_BUILD_DIR=$PWD/build KAS_RUNTIME_ARGS="--security-opt label=disable" kas-container build "kas/${1:-base}.yml"
