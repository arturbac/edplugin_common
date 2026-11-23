#!/usr/bin/env bash
# edplugin_common's test without the game, in new directories under the scratch dir: test/pin_test.sh (cmake/pin.cmake
# with git, without wine), then test/library_test.cc under wine.
# Usage: tools/test.sh <build dir> <scratch dir>; environment: EDPLUGIN_COMMON_WINEPREFIX (a wine prefix kept between
# runs), WINE_COMMON_TIMEOUT (seconds for one wine run, 600 unless set)
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$HERE/wine-common.sh"
BUILD=$(cd "${1:?build dir}" && pwd)
SCR=${2:?scratch dir}
[[ -f $BUILD/edplugin_common_test.exe ]] || { echo "$0: no $BUILD/edplugin_common_test.exe (build first)" >&2; exit 1; }
pin_rc=0
"$HERE/../test/pin_test.sh" "$(wine_common_run_dir "$SCR" edplugin_common-pin)" || pin_rc=$?
wine_common_prefix EDPLUGIN_COMMON_WINEPREFIX
RUN=$(wine_common_run_dir "$SCR" edplugin_common-test)
cp "$BUILD/edplugin_common_test.exe" "$RUN/"
rc=0
wine_common_run "$RUN" "$RUN/out.txt" -- edplugin_common_test.exe || rc=$?
cat "$RUN/out.txt"
((rc != 0 || pin_rc == 0)) || rc=$pin_rc
echo "=== overall: $([[ $rc -eq 0 ]] && echo PASSED || echo FAILED) (exit code $rc, run directory $RUN)"
exit $rc
