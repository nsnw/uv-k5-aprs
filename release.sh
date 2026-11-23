#!/bin/sh
# Build the release images from a clean tree: the full build and the
# receive-only build (ENABLE_TX=0), raw and packed, with SHA256SUMS, in dist/.
# Uses ./build.sh (Docker). On a tag, the tag must match VERSION in the Makefile.
# The images are reproducible: building the same commit again gives the same files.
set -eu
cd "$(dirname "$0")"

version=$(sed -n 's/^VERSION *?= *//p' Makefile)
tag=$(git describe --tags --exact-match 2>/dev/null || true)
if [ -n "$tag" ] && [ "$tag" != "v$version" ]; then
    echo "tag $tag doesn't match VERSION $version in the Makefile" >&2
    exit 1
fi

rm -rf dist
mkdir -p dist
for variant in full rx-only; do
    ./build.sh clean >/dev/null
    if [ "$variant" = full ]; then
        ./build.sh
        name=k5-aprs-$version
    else
        ./build.sh ENABLE_TX=0
        name=k5-aprs-$version-rx-only
    fi
    cp build/k5-aprs.bin "dist/$name.bin"
    cp build/k5-aprs.packed.bin "dist/$name.packed.bin"
done

cd dist
if command -v sha256sum >/dev/null; then
    sha256sum -- *.bin > SHA256SUMS
else
    shasum -a 256 -- *.bin > SHA256SUMS
fi
cat SHA256SUMS
