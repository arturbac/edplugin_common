#!/usr/bin/env bash
# The test of cmake/pin.cmake without wine: a library repository and a plugin repository with a clone of it, made in
# a new directory, and pin.cmake run on them for each case. Usage: test/pin_test.sh <run dir> (new and empty);
# prints ok/FAIL per case and PASSED or FAILED; exit code 0 only when all passed. git runs without the account's
# configuration (no identity, signing or hooks from it).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PIN_CMAKE=$HERE/../cmake/pin.cmake
RUN=$(realpath "${1:?run dir}")
[[ -d $RUN && -z $(ls -A "$RUN") ]] || { echo "$0: $RUN is not a new, empty directory" >&2; exit 2; }

# nothing of a repository around (GIT_DIR and the like, e.g. from a git hook) and no git search above the run dir
unset $(git rev-parse --local-env-vars)
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_CEILING_DIRECTORIES=$RUN
g() { git -c user.name=pin-test -c user.email=pin-test@localhost -c commit.gpgsign=false -c tag.gpgSign=false \
        -c init.defaultBranch=main -c core.hooksPath=/dev/null "$@"; }

failures=0
check()
  {
  if [[ $1 == 0 ]]; then echo "ok  : $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
  }

LIB=$RUN/lib
PLUGIN=$RUN/plugin
CLONE=$PLUGIN/edplugin_common
HEADER=$RUN/gen/edplugin_common_commit.h
mkdir -p "$LIB" "$PLUGIN" "$RUN/gen"

g -C "$LIB" init -q
echo one > "$LIB/file.txt"; g -C "$LIB" add file.txt; g -C "$LIB" commit -q -m one
C1=$(g -C "$LIB" rev-parse HEAD)
echo two > "$LIB/file.txt"; g -C "$LIB" commit -q -am two
C2=$(g -C "$LIB" rev-parse HEAD)

g -C "$PLUGIN" init -q
g clone -q "$LIB" "$CLONE"
echo /edplugin_common/ > "$PLUGIN/.gitignore"
echo "$C2" > "$PLUGIN/edplugin_common.commit"
g -C "$PLUGIN" add .gitignore edplugin_common.commit; g -C "$PLUGIN" commit -q -m plugin

# pin.cmake on the plugin: its output in $OUT, its exit code in $RC, the header's text in $TEXT
pin()
  {
  RC=0
  OUT=$(cmake -DCLONE="$CLONE" -DPIN="$PLUGIN/edplugin_common.commit" -DSRC="$PLUGIN" -DOUT="$HEADER" -P "$PIN_CMAKE" 2>&1) || RC=$?
  TEXT=$(sed -n 's/^#define EDPLUGIN_COMMON_COMMIT "\(.*\)"$/\1/p' "$HEADER" 2> /dev/null || true)
  }
warned() { [[ $OUT == *"CMake Warning"* ]]; }
failed() { [[ $RC != 0 && $OUT == *"CMake Error"* ]]; }
said() { [[ $OUT == *"$1"* ]]; }

S1=${C1:0:7}
S2=${C2:0:7}

pin
check $([[ $RC == 0 ]] && ! warned && [[ $TEXT == "$S2" ]]; echo $?) "the clone on the pinned commit: no warning, '$S2'"

before=$(stat -c %.Y "$HEADER")
pin
check $([[ $(stat -c %.Y "$HEADER") == "$before" ]]; echo $?) "a second run without a change leaves the header alone"

printf '%s\r\n' "$C2" > "$PLUGIN/edplugin_common.commit"
pin
check $([[ $RC == 0 ]] && ! warned && [[ $TEXT == "$S2" ]]; echo $?) "edplugin_common.commit with CRLF read as the commit"

echo "$C1" > "$PLUGIN/edplugin_common.commit"
pin
check $([[ $RC == 0 ]] && warned && said "checkout $C1" && [[ $TEXT == "$S2 (pinned $S1)" ]]; echo $?) \
  "the clone on another commit: a warning with the checkout, '$S2 (pinned $S1)'"
echo "$C2" > "$PLUGIN/edplugin_common.commit"

echo changed > "$CLONE/file.txt"
pin
check $([[ $RC == 0 ]] && warned && said "uncommitted changes" && [[ $TEXT == "$S2-dirty" ]]; echo $?) \
  "changes in the clone: a warning, '$S2-dirty'"
g -C "$CLONE" checkout -q -- file.txt

echo three > "$LIB/file.txt"; g -C "$LIB" commit -q -am three
C3=$(g -C "$LIB" rev-parse HEAD)
echo "$C3" > "$PLUGIN/edplugin_common.commit"
pin
check $([[ $RC == 0 ]] && warned && said "is not in the clone" && said "fetch origin" && [[ $TEXT == "$S2 (pinned ${C3:0:7})" ]]; echo $?) \
  "the pinned commit not in the clone: a warning with the fetch"
echo "$C2" > "$PLUGIN/edplugin_common.commit"

mv "$CLONE/.git" "$RUN/clone-git"
pin
check $([[ $RC == 0 ]] && warned && said "no git checkout of its own" && [[ $TEXT == unknown ]]; echo $?) \
  "a clone without .git inside the plugin's checkout: a warning, 'unknown'"
mv "$RUN/clone-git" "$CLONE/.git"

mkdir "$RUN/no-git-bin"
ln -s "$(command -v cmake)" "$RUN/no-git-bin/cmake"
RC=0
OUT=$(PATH=$RUN/no-git-bin cmake -DCLONE="$CLONE" -DPIN="$PLUGIN/edplugin_common.commit" -DSRC="$PLUGIN" -DOUT="$HEADER" \
        -P "$PIN_CMAKE" 2>&1) || RC=$?
TEXT=$(sed -n 's/^#define EDPLUGIN_COMMON_COMMIT "\(.*\)"$/\1/p' "$HEADER")
check $([[ $RC == 0 ]] && warned && said "git cannot read the clone" && [[ $TEXT == unknown ]]; echo $?) \
  "no git: a warning, 'unknown'"

pin
check $([[ $RC == 0 ]] && ! warned && [[ $TEXT == "$S2" ]]; echo $?) "back on the pinned commit before the release cases"

g -C "$PLUGIN" commit -q --allow-empty -m "release on the pinned commit"
g -C "$PLUGIN" tag -a v0.9.0 -m v0.9.0
pin
check $([[ $RC == 0 ]] && ! warned && [[ $TEXT == "$S2" ]]; echo $?) "a release (tag v0.9.0) with the clone on the pinned commit: no warning"

echo "$C1" > "$PLUGIN/edplugin_common.commit"
g -C "$PLUGIN" commit -q -am "pin one"
g -C "$PLUGIN" tag -a v1.0.0 -m v1.0.0
pin
check $(failed && said "release build" && said "checkout $C1"; echo $?) "a release (tag v1.0.0) with the clone on another commit: an error"

g -C "$PLUGIN" commit -q --allow-empty -m beta
g -C "$PLUGIN" tag -a v1.0.1-beta.1 -m v1.0.1-beta.1
pin
check $(failed && said "release build"; echo $?) "a pre-release (tag v1.0.1-beta.1) with the clone on another commit: an error"

g -C "$PLUGIN" rm -q edplugin_common.commit
g -C "$PLUGIN" commit -q -m "no pin"
pin
check $([[ $RC == 0 ]] && warned && said "pins no commit" && [[ $TEXT == unknown ]]; echo $?) \
  "no edplugin_common.commit outside a release: a warning, 'unknown'"
g -C "$PLUGIN" tag -a v1.0.2 -m v1.0.2
pin
check $(failed && said "pins no commit"; echo $?) "no edplugin_common.commit in a release: an error"

echo "=== pin_test: $([[ $failures == 0 ]] && echo PASSED || echo FAILED) ($failures failure(s), run directory $RUN)"
[[ $failures == 0 ]]
