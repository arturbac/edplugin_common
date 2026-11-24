#!/usr/bin/env bash
# edplugin_common's headers without a Windows header (test/native) built natively and run twice: with
# AddressSanitizer and UndefinedBehaviorSanitizer, then with ThreadSanitizer, each build in a new directory under the
# scratch dir. No wine and no MSVC.
# Usage: tools/test-native.sh <scratch dir>; environment: CXX (clang++ unless set), NATIVE_TIMEOUT (seconds for one
# configure, build or run, 600 unless set)
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SCR=$(realpath -m "${1:?scratch dir}")
mkdir -p "$SCR"
limit=(timeout -k 10 "${NATIVE_TIMEOUT:-600}")
rc=0
for sanitize in address,undefined thread; do
  dir=$(mktemp -d "$SCR/edplugin_common-native-${sanitize/,/-}.XXXXXX")
  echo "--- -fsanitize=$sanitize in $dir"
  if "${limit[@]}" cmake -S "$HERE/test/native" -B "$dir" -G Ninja -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
       -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEDPLUGIN_COMMON_NATIVE_SANITIZE="$sanitize" > "$dir/configure.txt" 2>&1 \
     && "${limit[@]}" cmake --build "$dir" > "$dir/build.txt" 2>&1; then
    run_rc=0
    "${limit[@]}" "$dir/edplugin_common_native_test" || run_rc=$?
    if [[ $run_rc -eq 124 || $run_rc -eq 137 ]]; then
      echo "-fsanitize=$sanitize: the run stopped by the limit of ${NATIVE_TIMEOUT:-600} s (exit code $run_rc)"
    fi
    [[ $run_rc -eq 0 ]] || rc=1
  else
    echo "-fsanitize=$sanitize: the configure or the build failed or hit the limit ($dir/configure.txt, $dir/build.txt)"
    rc=1
  fi
done
echo "=== overall: $([[ $rc -eq 0 ]] && echo PASSED || echo FAILED) (exit code $rc)"
exit $rc
