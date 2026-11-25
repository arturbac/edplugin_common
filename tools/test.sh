#!/usr/bin/env bash
# edplugin_common's test without the game, in new directories under the scratch dir: test/pin_test.sh (cmake/pin.cmake
# with git, without wine), test/version_test.sh (cmake/version.cmake with git, and the version resource of the build's
# test_exports.dll, without wine), test/proxy_exports_test.sh (edplugin_common_proxy_exports, without wine), then
# test/library_test.cc and, when built, tools/context_probe's test (context_probe_test.exe, no d3d11) and
# tools/event_report's (event_report_test.exe, and event_report.exe as a player runs it, in a prefix of its own by
# test/event_report_test.sh) under wine.
# Usage: tools/test.sh <build dir> <scratch dir>; environment: EDPLUGIN_COMMON_WINEPREFIX (a wine prefix kept between
# runs), WINE_COMMON_TIMEOUT (seconds for one wine run, 600 unless set)
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$HERE/wine-common.sh"
BUILD=$(cd "${1:?build dir}" && pwd)
SCR=${2:?scratch dir}
for built in edplugin_common_test.exe test_exports.dll; do
  [[ -f $BUILD/$built ]] || { echo "$0: no $BUILD/$built (build first)" >&2; exit 1; }
done
pin_rc=0
"$HERE/../test/pin_test.sh" "$(wine_common_run_dir "$SCR" edplugin_common-pin)" || pin_rc=$?
version_rc=0
"$HERE/../test/version_test.sh" "$(wine_common_run_dir "$SCR" edplugin_common-version)" "$BUILD" || version_rc=$?
exports_rc=0
"$HERE/../test/proxy_exports_test.sh" "$(wine_common_run_dir "$SCR" edplugin_common-proxy-exports)" || exports_rc=$?
wine_common_prefix EDPLUGIN_COMMON_WINEPREFIX
RUN=$(wine_common_run_dir "$SCR" edplugin_common-test)
cp "$BUILD/edplugin_common_test.exe" "$BUILD/test_exports.dll" "$RUN/"
rc=0
wine_common_run "$RUN" "$RUN/out.txt" -- edplugin_common_test.exe || rc=$?
cat "$RUN/out.txt"
# tools/context_probe's following of table entries and its hidden window, without d3d11, when the build's configure
# has it (EDPLUGIN_COMMON_CONTEXT_PROBE and EDPLUGIN_COMMON_TESTS; an exe left from an earlier configure is not run)
probe_rc=0
if grep -qx 'EDPLUGIN_COMMON_CONTEXT_PROBE:BOOL=ON' "$BUILD/CMakeCache.txt" \
  && grep -qx 'EDPLUGIN_COMMON_TESTS:BOOL=ON' "$BUILD/CMakeCache.txt"; then
  if [[ -f $BUILD/context_probe_test.exe ]]; then
    cp "$BUILD/context_probe_test.exe" "$RUN/"
    wine_common_run "$RUN" "$RUN/probe.txt" -- context_probe_test.exe || probe_rc=$?
    cat "$RUN/probe.txt"
  else
    echo "FAIL: no $BUILD/context_probe_test.exe though the configure has it (build first)"
    probe_rc=1
  fi
fi
# tools/event_report's part without Windows under Windows' comparison of names, and the program as a player runs it,
# when the build's configure has it (EDPLUGIN_COMMON_EVENT_REPORT and EDPLUGIN_COMMON_TESTS)
report_rc=0
if grep -qx 'EDPLUGIN_COMMON_EVENT_REPORT:BOOL=ON' "$BUILD/CMakeCache.txt" \
  && grep -qx 'EDPLUGIN_COMMON_TESTS:BOOL=ON' "$BUILD/CMakeCache.txt"; then
  if [[ -f $BUILD/event_report_test.exe && -f $BUILD/event_report.exe ]]; then
    cp "$BUILD/event_report_test.exe" "$RUN/"
    wine_common_run "$RUN" "$RUN/event_report.txt" -- event_report_test.exe || report_rc=$?
    cat "$RUN/event_report.txt"
    e2e_rc=0
    "$HERE/../test/event_report_test.sh" "$BUILD" "$(wine_common_run_dir "$SCR" edplugin_common-event-report)" \
      || e2e_rc=$?
    ((report_rc != 0 || e2e_rc == 0)) || report_rc=$e2e_rc
  else
    echo "FAIL: no $BUILD/event_report_test.exe or event_report.exe though the configure has them (build first)"
    report_rc=1
  fi
fi
# the first failure's code: the wine run's, then context_probe_test's, event_report's, pin_test's, version_test's,
# proxy_exports_test's
((rc != 0 || probe_rc == 0)) || rc=$probe_rc
((rc != 0 || report_rc == 0)) || rc=$report_rc
((rc != 0 || pin_rc == 0)) || rc=$pin_rc
((rc != 0 || version_rc == 0)) || rc=$version_rc
((rc != 0 || exports_rc == 0)) || rc=$exports_rc
echo "=== overall: $([[ $rc -eq 0 ]] && echo PASSED || echo FAILED) (exit code $rc, run directory $RUN)"
exit $rc
