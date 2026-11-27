#!/usr/bin/env bash
# The zip a player gets: context_probe.exe from the build directory, README.txt, the probe's own sources, the library's
# sources it uses (edplugin_common/), the library's LICENSE, SOURCE.txt (the library's commit) and SHA256SUMS.txt over
# them, in <output dir>/context_probe-7/ and <output dir>/context_probe-7.zip. Nothing else is changed; an output
# that is there already stops it. It stops too at a build that is not Release, a test build
# (EDPLUGIN_COMMON_UBSAN_TRAP), a repository with changes not committed, and an exe older than a source of it.
# Usage: tools/context_probe/package.sh <build dir> <output dir>
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BUILD=$(cd "${1:?build dir}" && pwd)
OUT=$(realpath -m "${2:?output dir}")
NAME=context_probe-7
EXE=$BUILD/context_probe.exe
PROBE=(README.txt context_probe.cc follow.cc follow.h hooks.cc hooks.h jump.h slots.h window.cc window.h
  context_probe.rc context_probe.manifest)
# the library's files the probe's sources reach (their includes, and the source of each header reached)
LIBRARY=(src/context.cc src/guard.cc src/log.cc src/memory.cc src/multithread.cc src/own_table.cc src/paths.cc src/pe.cc
  src/slot.cc src/system_d3d11.cc src/watch.cc include/edplugin_common/chain/system_d3d11.h
  include/edplugin_common/core/lock.h include/edplugin_common/core/once_flag.h include/edplugin_common/guard/guard.h
  include/edplugin_common/hook/context.h include/edplugin_common/hook/context_rule.h
  include/edplugin_common/hook/contract.h include/edplugin_common/hook/multithread.h
  include/edplugin_common/hook/own_table.h include/edplugin_common/hook/own_table_mark.h
  include/edplugin_common/hook/slot.h include/edplugin_common/hook/slot_plan.h include/edplugin_common/hook/watch.h
  include/edplugin_common/hook/watch_class.h include/edplugin_common/log/log.h include/edplugin_common/memory/memory.h
  include/edplugin_common/memory/pe.h include/edplugin_common/memory/pe_range.h include/edplugin_common/paths/paths.h
  include/edplugin_common/text/format.h)
command -v zip > /dev/null || { echo "$0: no zip" >&2; exit 1; }
[[ -f $EXE ]] || { echo "$0: no $EXE (build first)" >&2; exit 1; }
grep -qx 'CMAKE_BUILD_TYPE:STRING=Release' "$BUILD/CMakeCache.txt" \
  || { echo "$0: $BUILD is not a Release build" >&2; exit 1; }
if grep -qx 'EDPLUGIN_COMMON_UBSAN_TRAP:BOOL=ON' "$BUILD/CMakeCache.txt"; then
  echo "$0: $BUILD is a test build (EDPLUGIN_COMMON_UBSAN_TRAP)" >&2
  exit 1
fi
[[ -z $(git -C "$ROOT" status --porcelain) ]] \
  || { echo "$0: $ROOT has changes not committed (the zip's sources are the commit's)" >&2; exit 1; }
for f in "${PROBE[@]/#/$HERE/}" "${LIBRARY[@]/#/$ROOT/}"; do
  [[ -f $f ]] || { echo "$0: no $f" >&2; exit 1; }
  [[ ! $f -nt $EXE ]] || { echo "$0: $f is newer than $EXE (build again)" >&2; exit 1; }
done
[[ ! -e $OUT/$NAME && ! -e $OUT/$NAME.zip ]] || { echo "$0: $OUT/$NAME or its zip is there already" >&2; exit 1; }
mkdir -p "$OUT/$NAME/edplugin_common"
cp "$EXE" "${PROBE[@]/#/$HERE/}" "$ROOT/LICENSE" "$OUT/$NAME/"
for f in "${LIBRARY[@]}"; do
  cp "$ROOT/$f" "$OUT/$NAME/edplugin_common/"
done
echo "edplugin_common $(git -C "$ROOT" rev-parse HEAD)" > "$OUT/$NAME/SOURCE.txt"
(cd "$OUT/$NAME" && find . -type f ! -name SHA256SUMS.txt | LC_ALL=C sort | sed 's|^\./||' \
  | xargs sha256sum -- > SHA256SUMS.txt)
(cd "$OUT" && zip -q -r -X "$NAME.zip" "$NAME")
echo "$OUT/$NAME.zip $(sha256sum "$OUT/$NAME.zip" | cut -d' ' -f1)"
