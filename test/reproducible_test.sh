#!/usr/bin/env bash
# The test of the reproducible link without wine: test/reproducible (a dll built as a plugin's) configured with the
# toolchain, MSVC_ROOT and build type of a build directory, built in a new build directory and built again from clean a
# second later; the two dlls have to be the same, with the time stamp of the library's HEAD commit, and the dll's debug
# directory names the PDB by its file name only; then built again with SOURCE_DATE_EPOCH, which relinks it with that
# time stamp. (Another build directory gives another dll: the objects' paths are in the PDB, whose hash is the PDB's
# id.) Usage:
# test/reproducible_test.sh <run dir> (new and empty) <build dir>; prints ok/FAIL per case and PASSED or FAILED; exit
# code 0 only when all passed.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RUN=$(realpath "${1:?run dir}")
BUILD=$(realpath "${2:?build dir}")
[[ -d $RUN && -z $(ls -A "$RUN") ]] || { echo "$0: $RUN is not a new, empty directory" >&2; exit 2; }
unset SOURCE_DATE_EPOCH

failures=0
check()
  {
  if [[ $1 == 0 ]]; then echo "ok  : $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
  }

# a cache entry of the build directory
cached() { sed -n "s/^$1:[A-Z]*=//p" "$BUILD/CMakeCache.txt"; }
TOOLCHAIN=$(cached CMAKE_TOOLCHAIN_FILE)
MSVC=$(cached MSVC_ROOT)
TYPE=$(cached CMAKE_BUILD_TYPE)

DIR=$RUN/build
A=$RUN/first.dll
B=$DIR/reproducible.dll
if cmake -S "$HERE/reproducible" -B "$DIR" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DMSVC_ROOT="$MSVC" \
     -DCMAKE_BUILD_TYPE="$TYPE" > "$RUN/configure.txt" 2>&1 \
   && cmake --build "$DIR" > "$RUN/build-first.txt" 2>&1 && cp "$B" "$A" && sleep 1 \
   && cmake --build "$DIR" --clean-first > "$RUN/build-again.txt" 2>&1; then
  check 0 "built twice in $DIR, the second time from clean"
else
  check 1 "built twice ($RUN/configure.txt, $RUN/build-first.txt, $RUN/build-again.txt)"
fi

if [[ -f $A && -f $B ]]; then
  # the differing bytes kept for a look when they differ
  cmp -l "$A" "$B" > "$RUN/differences.txt" || true
  check $([[ ! -s $RUN/differences.txt ]]; echo $?) \
    "the two builds' dlls are the same ($(sha256sum < "$A" | cut -c1-16)..., $(sha256sum < "$B" | cut -c1-16)...)"
  READOBJ=$(command -v llvm-readobj || true)
  debug=""
  resources=""
  headers=""
  if [[ -n $READOBJ ]]; then
    debug=$("$READOBJ" --coff-debug-directory "$A" || true)
    resources=$("$READOBJ" --coff-resources "$A" || true)
    headers=$("$READOBJ" --file-headers "$A" || true)
  fi
  check $(grep -qE '^ *PDBFileName: reproducible\.pdb$' <<< "$debug"; echo $?) \
    "the debug directory names reproducible.pdb, not a path"
  check $(grep -qF 'Type: VERSIONINFO (ID 16)' <<< "$resources"; echo $?) "the dll has its version resource"
  commit_time=$(git -C "$HERE/reproducible" log -1 --no-show-signature --format=%ct HEAD 2> /dev/null || true)
  if [[ -n $commit_time ]]; then
    check $(grep -qE "^ *TimeDateStamp: .*\\(0x$(printf '%X' "$commit_time")\\)$" <<< "$headers"; echo $?) \
      "the dll's time stamp is the commit's time ($commit_time)"
  else
    echo "skip: the library is no git checkout, no commit's time to compare the time stamp with"
  fi
  # the relink comes from the forced-in version header too, so this case holds without the link's LINK_DEPENDS
  if SOURCE_DATE_EPOCH=1000000000 cmake --build "$DIR" > "$RUN/build-epoch.txt" 2>&1; then
    epoch_headers=""
    [[ -z $READOBJ ]] || epoch_headers=$("$READOBJ" --file-headers "$B" || true)
    check $(grep -qE '^ *TimeDateStamp: .*\(0x3B9ACA00\)$' <<< "$epoch_headers"; echo $?) \
      "built again with SOURCE_DATE_EPOCH=1000000000: relinked with that time stamp"
  else
    check 1 "built again with SOURCE_DATE_EPOCH ($RUN/build-epoch.txt)"
  fi
fi

echo "=== reproducible_test: $([[ $failures == 0 ]] && echo PASSED || echo FAILED) ($failures failure(s), run directory $RUN)"
[[ $failures == 0 ]]
