#!/usr/bin/env bash
# The test of event_report.exe as a player runs it, under wine, in a wine prefix of its own made in the run directory
# (nothing put into a prefix kept between runs): a saved report of Windows Error Reporting for a crash of the game
# (with the prefix user's profile path and name in fields the report shows and a file loaded from that profile) and a
# crash dump are put in the prefix's folders for them, the program runs, and its result file is checked: made, its head,
# the loaded file told from elsewhere, the dump's name, the user's and the computer's names replaced and nowhere in the
# text, lines ending with CR LF. The prefix's logs are wine's (likely without the event log): the lines saying so are
# not checked. Usage: test/event_report_test.sh <build dir> <run dir> (new and empty); prints ok/FAIL per check and
# PASSED or FAILED; exit code 0 only when all passed.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
. "$HERE/../tools/wine-common.sh"
BUILD=$(cd "${1:?build dir}" && pwd)
RUN=$(realpath "${2:?run dir}")
[[ -d $RUN && -z $(ls -A "$RUN") ]] || { echo "$0: $RUN is not a new, empty directory" >&2; exit 2; }
[[ -f $BUILD/event_report.exe ]] || { echo "$0: no $BUILD/event_report.exe (build first)" >&2; exit 2; }

failures=0
check()
  {
  if [[ $1 == 0 ]]; then echo "ok  : $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
  }
expect()
  {
  local what=$1
  shift
  if "$@"; then check 0 "$what"; else check 1 "$what"; fi
  }

export WINEPREFIX=$RUN/prefix
WINEDEBUG=-all timeout -k 10 "${WINE_COMMON_TIMEOUT:-600}" wineboot -i > "$RUN/wineboot.txt" 2>&1 < /dev/null \
  || { echo "$0: wineboot failed ($RUN/wineboot.txt)" >&2; exit 2; }
# the prefix's user: the folder under drive_c/users with AppData (wine names it after the account, Proton's wine
# "steamuser")
user=
for dir in "$WINEPREFIX"/drive_c/users/*/; do
  name=$(basename "$dir")
  [[ $name != Public && -d $dir/AppData/Local ]] && { user=$name; break; }
done
[[ -n $user ]] || { echo "$0: no user folder with AppData in $WINEPREFIX/drive_c/users" >&2; exit 2; }
local_dir=$WINEPREFIX/drive_c/users/$user/AppData/Local
report_dir=$local_dir/Microsoft/Windows/WER/ReportArchive/AppCrash_EliteDangerous64_event_report_test
dump_dir="$local_dir/Temp/Frontier Developments/EliteDangerous/CrashDumps"
mkdir -p "$report_dir" "$dump_dir"
{
  printf '\xff\xfe'
  printf '%s\r\n' 'Version=1' 'EventType=APPCRASH' 'Sig[0].Name=Application Name' \
    'Sig[0].Value=EliteDangerous64.exe' 'Sig[1].Name=Test path' "Sig[1].Value=C:\\users\\$user\\Desktop\\x" \
    'Sig[2].Name=Seen by' "Sig[2].Value=$user test" \
    'LoadedModule[0]=C:\Games\ED\EliteDangerous64.exe' 'LoadedModule[1]=C:\windows\system32\ntdll.dll' \
    "LoadedModule[2]=C:\\users\\$user\\AppData\\Local\\inject_test.dll" 'ReportIdentifier=00000000-event-report-test' \
    'AppName=EliteDangerous64.exe' 'AppPath=C:\Games\ED\EliteDangerous64.exe' | iconv -f UTF-8 -t UTF-16LE
} > "$report_dir/Report.wer"
printf 'MDMP' > "$dump_dir/EliteDangerous64.exe.4242.dmp"

mkdir "$RUN/exe"
cp "$BUILD/event_report.exe" "$RUN/exe/"
rc=0
wine_common_run "$RUN/exe" "$RUN/console.txt" -- event_report.exe || rc=$?
check "$rc" "event_report.exe ends with 0 (exit code $rc)"
shopt -s nullglob
results=("$RUN"/exe/event_report-*.txt)
shopt -u nullglob
check "$((${#results[@]} == 1 ? 0 : 1))" "one result file event_report-<date>-<time>.txt (${#results[@]})"
result=${results[0]:-/dev/null}
has() { grep -qF -- "$1" "$result"; }
has_not() { ! grep -qiF -- "$1" "$result"; }
expect "the head" has 'event_report 1 - Elite Dangerous events from Windows'"'"' logs'
expect "the loaded file from the user's folder told as from elsewhere" has 'from elsewhere:     inject_test.dll'
expect "the file from the Windows folder counted" has 'and 1 from the Windows folder'
expect "a path in a field shown as its file name" has 'Test path: x (its folder left out)'
expect "the user's name replaced in a field shown" has 'Seen by: <user> test'
expect "the crash dump's name" has 'EliteDangerous64.exe.4242.dmp'
expect "the prefix's user name ($user) nowhere" has_not "$user"
expect "the computer's name nowhere" has_not "$(hostname)"
if ((${#USER} >= 4)); then
  expect "the account's name nowhere" has_not "$USER"
fi
expect "lines end with CR LF" grep -q $'\r$' "$result"
WINEDEBUG=-all timeout -k 10 60 wineserver -k > /dev/null 2>&1 || true

verdict=$([[ $failures -eq 0 ]] && echo PASSED || echo FAILED)
echo "=== event_report_test: $verdict ($failures failure(s), run directory $RUN)"
((failures == 0))
