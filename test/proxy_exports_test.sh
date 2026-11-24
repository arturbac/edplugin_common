#!/usr/bin/env bash
# The test of edplugin_common_proxy_exports without wine: the function run by cmake -P (test/proxy_exports/run.cmake)
# on lists made here, each case in a new directory under the run dir. Usage: test/proxy_exports_test.sh <run dir>
# (new and empty); prints ok/FAIL per case and PASSED or FAILED; exit code 0 only when all passed.
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
RUN_CMAKE=$HERE/proxy_exports/run.cmake
EXPECTED=$HERE/proxy_exports
RUN=$(realpath "${1:?run dir}")
[[ -d $RUN && -z $(ls -A "$RUN") ]] || { echo "$0: $RUN is not a new, empty directory" >&2; exit 2; }

failures=0
check()
  {
  if [[ $1 == 0 ]]; then echo "ok  : $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
  }

# a list with CRLF, comments (a line of its own, after a name), spaces and an empty line, the wrapped names among the rest
good_list() { printf '# a list for the test\r\nAlpha\r\nBeta   # a comment after a name\r\n\r\n  Wrapped1\r\nGamma\r\nWrapped2\r\n'; }

# a new case directory in $CASE with the list from stdin as list.txt
new_case() { CASE=$RUN/$1; mkdir "$CASE"; cat > "$CASE/list.txt"; }

# the function in $CASE: output in $OUT on one line (CMake wraps an error's text), exit code in $RC
px()
  {
  RC=0
  OUT=$(cd "$CASE" && cmake "$@" -P "$RUN_CMAKE" 2>&1) || RC=$?
  OUT=$(printf '%s' "$OUT" | tr '\n' ' ' | tr -s ' ')
  }
said() { [[ $OUT == *"$1"* ]]; }
# nothing but gen/ (with nothing in it besides the given files) in the case directory next to list.txt
only_made()
  {
  local found expected
  found=$(cd "$CASE" && find . -mindepth 1 ! -name list.txt ! -name extra.txt | sort | tr '\n' ' ')
  expected=$(for f in . "$@"; do [[ $f == . ]] && echo ./gen || echo "./gen/$f"; done | sort | tr '\n' ' ')
  [[ $found == "$expected" ]]
  }
# an error: exit code not 0, the message, and no output file left
refused() { [[ $RC != 0 ]] && said "$1" && [[ -z $(ls -A "$CASE/gen" 2> /dev/null) ]]; }

new_case good < <(good_list)
px -DTAG=t1 -DLIST=list.txt "-DWRAP=Wrapped1;Wrapped2"
check $([[ $RC == 0 ]] && cmp -s "$CASE/gen/t1_proxy.def" "$EXPECTED/expected_t1_proxy.def" \
          && cmp -s "$CASE/gen/t1_proxy_thunks.asm" "$EXPECTED/expected_t1_proxy_thunks.asm" \
          && cmp -s "$CASE/gen/t1_proxy.h" "$EXPECTED/expected_t1_proxy.h"; echo $?) \
  "a good list: the three files as expected"
check $(only_made t1_proxy.def t1_proxy_thunks.asm t1_proxy.h; echo $?) "nothing made but the three files in gen/"
check $(said "def $CASE/gen/t1_proxy.def" && said "asm $CASE/gen/t1_proxy_thunks.asm" \
          && said "header $CASE/gen/t1_proxy.h"; echo $?) "the variables name the three files"

# the k-th name of t1_proxy_names and the name exported by t1_proxy_thunk_<k> are the same
# (|| true: a missing file or no match fails the check below, not the whole test)
names=$(sed -n 's/^  "\(.*\)",$/\1/p' "$CASE/gen/t1_proxy.h" | tr '\n' ' ') || true
thunks=$(sed -n 's/^    \([A-Za-z0-9_]*\) = t1_proxy_thunk_\([0-9]*\)$/\2 \1/p' "$CASE/gen/t1_proxy.def" | sort -n \
           | cut -d' ' -f2 | tr '\n' ' ') || true
slots=$(grep -c '^    jmp QWORD PTR \[t1_proxy_real_procs + ' "$CASE/gen/t1_proxy_thunks.asm") || true
check $([[ $names == "Alpha Beta Gamma " && $thunks == "$names" && $slots == 3 ]] \
          && grep -q 'QWORD 3 DUP(0)' "$CASE/gen/t1_proxy_thunks.asm"; echo $?) \
  "slot k: the k-th name and thunk k agree, the wrapped names have no slot"

before=$(stat -c %.Y "$CASE/gen/t1_proxy.def" "$CASE/gen/t1_proxy_thunks.asm" "$CASE/gen/t1_proxy.h") || true
px -DTAG=t1 -DLIST=list.txt "-DWRAP=Wrapped1;Wrapped2"
check $([[ $RC == 0 && $(stat -c %.Y "$CASE/gen/t1_proxy.def" "$CASE/gen/t1_proxy_thunks.asm" "$CASE/gen/t1_proxy.h") == "$before" ]]
        echo $?) "a second run without a change leaves the files alone"

new_case two-tags < <(good_list)
printf 'One\nTwo\n' > "$CASE/extra.txt"
px -DTAG=t1 -DLIST=list.txt "-DWRAP=Wrapped1;Wrapped2" -DTAG2=t2 -DLIST2=extra.txt
check $([[ $RC == 0 ]] && said "def2 $CASE/gen/t2_proxy.def" && grep -q 'QWORD 2 DUP(0)' "$CASE/gen/t2_proxy_thunks.asm" \
          && cmp -s "$CASE/gen/t1_proxy.h" "$EXPECTED/expected_t1_proxy.h"; echo $?) \
  "two tags in one directory: files and variables of each"

# names that CMake's if() reads as false constants; WRAP given against the list's order
new_case false-names < <(printf 'Off\nGamma\nNo\nN\n')
px -DTAG=t1 -DLIST=list.txt "-DWRAP=N;No"
check $([[ $RC == 0 ]] && grep -qx '    Off = t1_proxy_thunk_0' "$CASE/gen/t1_proxy.def" \
          && [[ $(grep -c ' = t1_proxy_' "$CASE/gen/t1_proxy.def") == 4 ]] \
          && [[ $(grep '_proxy_impl_' "$CASE/gen/t1_proxy.def" | tr -d ' \n') == "No=t1_proxy_impl_NoN=t1_proxy_impl_N" ]]
        echo $?) "names such as Off and No: each exported once, the wrapped ones in the list's order"
new_case one-false-name < <(printf 'Off\n')
px -DTAG=t1 -DLIST=list.txt
check $([[ $RC == 0 ]] && grep -q 'QWORD 1 DUP(0)' "$CASE/gen/t1_proxy_thunks.asm"; echo $?) "a list of the single name Off"

new_case twice < <(printf 'Alpha\n\nBeta\nAlpha\n')
px -DTAG=t1 -DLIST=list.txt
check $(refused "list.txt:4: 'Alpha' listed twice"; echo $?) "a name twice: an error at its line (after an empty line)"

new_case bad-name < <(printf 'Alpha\nBeta # ok\n\n2nd\n')
px -DTAG=t1 -DLIST=list.txt
check $(refused "list.txt:4: '2nd' is not a C identifier"; echo $?) "a name that is not a C identifier: an error at its line"

new_case bad-tag < <(printf 'Alpha\n')
px -DTAG=1x -DLIST=list.txt
check $(refused "the tag '1x' is not a C identifier"; echo $?) "a tag that is not a C identifier: an error"

new_case wrap-missing < <(printf 'Alpha\nBeta\n')
px -DTAG=t1 -DLIST=list.txt -DWRAP=Gamma
check $(refused "WRAP Gamma: not in"; echo $?) "a WRAP name not in the list: an error"

new_case wrap-twice < <(printf 'Alpha\nBeta\n')
px -DTAG=t1 -DLIST=list.txt "-DWRAP=Beta;Beta"
check $(refused "WRAP Beta given twice"; echo $?) "a WRAP name twice: an error"

new_case empty < <(printf '# nothing\n\n')
px -DTAG=t1 -DLIST=list.txt
check $(refused "no name in the list"; echo $?) "a list without names: an error"

new_case all-wrapped < <(printf 'Alpha\nBeta\n')
px -DTAG=t1 -DLIST=list.txt "-DWRAP=Alpha;Beta"
check $(refused "no thunk left"; echo $?) "every name wrapped: an error"

new_case semicolon < <(printf 'Alpha\nBeta;Gamma\n')
px -DTAG=t1 -DLIST=list.txt
check $(refused "list.txt:2: ';' in the list"; echo $?) "';' in a line: an error at its line"

# ';', '[', ']' and a byte outside ASCII in comments: allowed, and an error after them at its own line
new_case comment-marks < <(printf '# a list; [with] marks \xc5\x82\nAlpha # Beta;Gamma \xc3\xa9\n\nBeta\n')
px -DTAG=t1 -DLIST=list.txt
check $([[ $RC == 0 ]] && grep -q 'QWORD 2 DUP(0)' "$CASE/gen/t1_proxy_thunks.asm"; echo $?) \
  "';', '[', ']' and a byte outside ASCII in comments: allowed"
new_case comment-marks-then-error < <(printf '# a list; [with] marks \xc5\x82\nAlpha # x;y\n\nBeta;Gamma\n')
px -DTAG=t1 -DLIST=list.txt
check $(refused "list.txt:4: ';' in the list"; echo $?) "';' after comments with marks: an error at its own line"

new_case non-ascii < <(printf 'Alpha\n\nBeta\xc3\xa9\n')
px -DTAG=t1 -DLIST=list.txt
check $(refused "list.txt:3: a character outside ASCII"; echo $?) "a byte outside ASCII: an error at its line"

echo "=== proxy_exports_test: $([[ $failures == 0 ]] && echo PASSED || echo FAILED) ($failures failure(s), run directory $RUN)"
[[ $failures == 0 ]]
