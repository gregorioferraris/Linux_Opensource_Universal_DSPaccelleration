#!/bin/bash
# DSPA Version 1.0.0 Release Packager
set -e

VERSION=$(cat VERSION)
PKG_NAME="dspa-linux-v$VERSION"

echo "--- Packaging DSPA $VERSION ---"
mkdir -p $PKG_NAME/bin
mkdir -p $PKG_NAME/docs

# Copy Binaries
cp build/daemon/pipewire/*.so $PKG_NAME/bin/
cp build/daemon/pipewire/dsp-accel-worker $PKG_NAME/bin/
cp build/dsp_shader.spv $PKG_NAME/bin/

# Copy Documentation
cp docs/*.md $PKG_NAME/docs/
cp README.md $PKG_NAME/

tar -czvf $PKG_NAME.tar.gz $PKG_NAME
echo "SUCCESS: $PKG_NAME.tar.gz is ready for GitHub Release."