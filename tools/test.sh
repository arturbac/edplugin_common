#!/usr/bin/env bash
# edplugin_common's test without the game: test/library_test.cc under wine, in a new directory under the scratch dir.
# Usage: tools/test.sh <build dir> <scratch dir>; environment: EDPLUGIN_COMMON_WINEPREFIX (a wine prefix kept between
# runs), WINE_COMMON_TIMEOUT (seconds for one wine run, 600 unless set)
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$HERE/wine-common.sh"
BUILD=$(cd "${1:?build dir}" && pwd)
SCR=${2:?scratch dir}
[[ -f $BUILD/edplugin_common_test.exe ]] || { echo "$0: no $BUILD/edplugin_common_test.exe (build first)" >&2; exit 1; }
wine_common_prefix EDPLUGIN_COMMON_WINEPREFIX
RUN=$(wine_common_run_dir "$SCR" edplugin_common-test)
cp "$BUILD/edplugin_common_test.exe" "$RUN/"
rc=0
wine_common_run "$RUN" "$RUN/out.txt" -- edplugin_common_test.exe || rc=$?
cat "$RUN/out.txt"
echo "=== overall: $([[ $rc -eq 0 ]] && echo PASSED || echo FAILED) (exit code $rc, run directory $RUN)"
exit $rc
