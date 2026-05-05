#!/bin/bash
set -e
export KAS_BUILD_DIR=$PWD/build
export KAS_RUNTIME_ARGS="--security-opt label=disable"
kas-container shell kas/base.yml -c "bitbake -c cleansstate linux-tegra && bitbake -c build banks-jetson-image-base"
