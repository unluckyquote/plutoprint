#!/bin/sh
# Downloads the pinned third-party browser libraries used by the installer page, checks their checksums
# and unpacks the few files needed, with their licences. The page then loads nothing from other sites.
# usage: tools/vendor-web-libs.sh [output directory, default web/vendor]
set -eu

OUT="${1:-web/vendor}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

sha256() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }

fetch() { # name version sha256
  curl -fsSL "https://registry.npmjs.org/$1/-/$1-$2.tgz" -o "$WORK/$1.tgz"
  if [ "$(sha256 "$WORK/$1.tgz")" != "$3" ]; then echo "checksum mismatch for $1 $2" >&2; exit 1; fi
  mkdir -p "$WORK/$1" && tar xzf "$WORK/$1.tgz" -C "$WORK/$1"
}

fetch esp-web-tools 10.5.2 35681b4f1910a95f51a592ad9701f059ff60d2902ad94df9e1ad7ee59ab6b7ca
fetch three 0.160.0 1ee2f935c4f555814b388e87b5ef78a44856bd2e9d0feb88643a6e193fb42856

rm -rf "$OUT"
mkdir -p "$OUT/esp-web-tools" "$OUT/three/build" "$OUT/three/examples/jsm/loaders" "$OUT/three/examples/jsm/controls"
cp "$WORK"/esp-web-tools/package/dist/web/*.js "$OUT/esp-web-tools/"
cp "$WORK/esp-web-tools/package/LICENSE" "$OUT/esp-web-tools/LICENSE"
cp "$WORK/three/package/build/three.module.js" "$OUT/three/build/"
cp "$WORK/three/package/examples/jsm/loaders/STLLoader.js" "$OUT/three/examples/jsm/loaders/"
cp "$WORK/three/package/examples/jsm/controls/OrbitControls.js" "$OUT/three/examples/jsm/controls/"
cp "$WORK/three/package/LICENSE" "$OUT/three/LICENSE"
echo "Libraries written to $OUT"
