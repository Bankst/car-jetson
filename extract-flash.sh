#!/bin/bash
set -e
sudo rm -rf /tmp/flash
sudo mkdir -p /tmp/flash && sudo chown -R  bankst:bankst /tmp/flash
tar xzf build/tmp/deploy/images/jetson-xavier-nx-a203/banks-jetson-image-base-jetson-xavier-nx-a203.rootfs.tegraflash.tar.gz -C /tmp/flash
echo "Extracted to /tmp/flash"
