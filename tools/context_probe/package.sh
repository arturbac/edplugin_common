#!/usr/bin/env bash
# The zip a player gets: context_probe.exe from the build directory, README.txt, the probe's own sources and
# SHA256SUMS.txt over them, in <output dir>/context_probe-5/ and <output dir>/context_probe-5.zip. Nothing else is
# changed; an output that is there already stops it.
# Usage: tools/context_probe/package.sh <build dir> <output dir>
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD=$(cd "${1:?build dir}" && pwd)
OUT=$(realpath -m "${2:?output dir}")
NAME=context_probe-5
[[ -f $BUILD/context_probe.exe ]] || { echo "$0: no $BUILD/context_probe.exe (build first)" >&2; exit 1; }
[[ ! -e $OUT/$NAME && ! -e $OUT/$NAME.zip ]] || { echo "$0: $OUT/$NAME or its zip is there already" >&2; exit 1; }
mkdir -p "$OUT/$NAME"
cp "$BUILD/context_probe.exe" "$OUT/$NAME/"
for f in README.txt context_probe.cc follow.cc follow.h jump.h slots.h context_probe.rc context_probe.manifest; do
  cp "$HERE/$f" "$OUT/$NAME/"
done
(cd "$OUT/$NAME" && sha256sum -- * > SHA256SUMS.txt)
(cd "$OUT" && zip -q -r -X "$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip $(sha256sum "$OUT/$NAME.zip" | cut -d' ' -f1)"
