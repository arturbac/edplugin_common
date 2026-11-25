#!/usr/bin/env bash
# The test of cmake/version.cmake without wine: a plugin repository made in a new directory, and version.cmake run on it
# for each case (the header, and the version resource of an exe and a dll); with a build directory, also the version
# resource linked into its test_exports.dll (llvm-readobj and strings, no wine). Usage: test/version_test.sh <run dir>
# (new and empty) [<build dir>]; prints ok/FAIL per case and PASSED or FAILED; exit code 0 only when all passed. git
# runs without the account's configuration (no identity, signing or hooks from it).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
VERSION_CMAKE=$HERE/../cmake/version.cmake
RUN=$(realpath "${1:?run dir}")
BUILD=${2:-}
[[ -d $RUN && -z $(ls -A "$RUN") ]] || { echo "$0: $RUN is not a new, empty directory" >&2; exit 2; }

# nothing of a repository around (GIT_DIR and the like, e.g. from a git hook) and no git search above the run dir
unset $(git rev-parse --local-env-vars)
unset SOURCE_DATE_EPOCH
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 GIT_CEILING_DIRECTORIES=$RUN
# commits and tags at a known time: 2025-11-24T23:12:05Z
export GIT_COMMITTER_DATE="@1764025925 +0000" GIT_AUTHOR_DATE="@1764025925 +0000"
TIME=2025-11-24T23:12:05Z
g() { git -c user.name=version-test -c user.email=version-test@localhost -c commit.gpgsign=false -c tag.gpgSign=false \
        -c init.defaultBranch=main -c core.hooksPath=/dev/null "$@"; }

failures=0
check()
  {
  if [[ $1 == 0 ]]; then echo "ok  : $2"; else echo "FAIL: $2"; failures=$((failures + 1)); fi
  }

PLUGIN=$RUN/plugin
GEN=$RUN/gen
HEADER=$GEN/plugin_version.h
mkdir -p "$PLUGIN" "$GEN"

# version.cmake on a checkout ($1, the plugin unless given) for plugin.dll and plugin_tool.exe: its exit code in $RC,
# the header's macros in $VERSION, $COMMIT, $TIME_OF, the dll's resource in $RC_DLL, the exe's in $RC_EXE
version()
  {
  RC=0
  cmake -DSRC="${1:-$PLUGIN}" -DOUT="$HEADER" -DNAME=PLUGIN_VERSION -DRC_DIR="$GEN" -DRC_TARGETS=plugin,plugin_tool \
    -DRC_FILES=plugin.dll,plugin_tool.exe -DRC_TYPES=2,1 -DPRODUCT="${PRODUCT:-plugin}" -P "$VERSION_CMAKE" \
    > "$RUN/out.txt" 2>&1 || RC=$?
  VERSION=$(sed -n 's/^#define PLUGIN_VERSION "\(.*\)"$/\1/p' "$HEADER")
  COMMIT=$(sed -n 's/^#define PLUGIN_VERSION_COMMIT "\(.*\)"$/\1/p' "$HEADER")
  TIME_OF=$(sed -n 's/^#define PLUGIN_VERSION_BUILD_TIME "\(.*\)"$/\1/p' "$HEADER")
  RC_DLL=$(cat "$GEN/plugin_version.rc")
  RC_EXE=$(cat "$GEN/plugin_tool_version.rc")
  }
# the resource ($1) has the line $2
has() { grep -qxF -- "$2" <<< "$1"; }
# the resource ($1) for the version $2 with the numbers $3 and the flags $4 (PrivateBuild with 0x8 and 0xa only)
resource()
  {
  has "$1" "FILEVERSION $3" && has "$1" "PRODUCTVERSION $3" && has "$1" "FILEFLAGS $4" \
    && has "$1" "      VALUE \"FileVersion\", \"$2\"" && has "$1" "      VALUE \"ProductVersion\", \"$2\"" \
    && if [[ $4 == 0x8 || $4 == 0xa ]]; then has "$1" "      VALUE \"PrivateBuild\", \"$2\""
       else ! grep -q PrivateBuild <<< "$1"; fi
  }

g -C "$PLUGIN" init -q
echo one > "$PLUGIN/file.txt"; g -C "$PLUGIN" add file.txt; g -C "$PLUGIN" commit -q -m one
C1=$(g -C "$PLUGIN" rev-parse HEAD)
S1=${C1:0:7}

version
check $([[ $RC == 0 && $VERSION == "v0.0.0-$S1" && $COMMIT == "$C1" && $TIME_OF == "$TIME" ]]; echo $?) \
  "before the first tag: 'v0.0.0-$S1', the full commit, the commit's time"
check $(resource "$RC_DLL" "v0.0.0-$S1" 0,0,0,1 0x8; echo $?) "before the first tag: 0,0,0,1 (all commits), PRIVATEBUILD"
check $(has "$RC_DLL" "FILETYPE 2" && has "$RC_DLL" "      VALUE \"OriginalFilename\", \"plugin.dll\"" \
          && has "$RC_DLL" "      VALUE \"InternalName\", \"plugin\"" && has "$RC_DLL" "      VALUE \"ProductName\", \"plugin\""
        echo $?) "the dll's resource: FILETYPE 2, its file and target names, the product"
check $(has "$RC_EXE" "FILETYPE 1" && has "$RC_EXE" "      VALUE \"OriginalFilename\", \"plugin_tool.exe\"" \
          && resource "$RC_EXE" "v0.0.0-$S1" 0,0,0,1 0x8; echo $?) "the exe's resource: FILETYPE 1, its file name"

before=$(stat -c %.Y "$HEADER" "$GEN/plugin_version.rc" "$GEN/plugin_tool_version.rc")
version
check $([[ $(stat -c %.Y "$HEADER" "$GEN/plugin_version.rc" "$GEN/plugin_tool_version.rc") == "$before" ]]; echo $?) \
  "a second run without a change leaves the header and the resources alone"

SOURCE_DATE_EPOCH=0 version
check $([[ $RC == 0 && $TIME_OF == 1970-01-01T00:00:00Z ]]; echo $?) "SOURCE_DATE_EPOCH=0: its time, not the commit's"

SOURCE_DATE_EPOCH=yesterday version
check $([[ $RC == 0 && $TIME_OF == "$TIME" ]]; echo $?) "SOURCE_DATE_EPOCH not a number: the commit's time"

g -C "$PLUGIN" tag v2
version
check $([[ $RC == 0 && $VERSION == v2 && $(cat "$RUN/out.txt") == *"not vX.Y.Z"* ]] \
          && resource "$RC_DLL" v2 0,0,0,0 0x0; echo $?) "a tag not vX.Y.Z (v2) on HEAD: 'v2', 0,0,0,0 with a warning"
g -C "$PLUGIN" tag -d v2 > /dev/null

echo changed > "$PLUGIN/file.txt"
version
check $([[ $RC == 0 && $VERSION == "v0.0.0-$S1-dirty" && $TIME_OF == "$TIME-dirty" ]] \
          && resource "$RC_DLL" "v0.0.0-$S1-dirty" 0,0,0,1 0x8; echo $?) \
  "uncommitted changes: '-dirty' on the version and the time, PRIVATEBUILD"
g -C "$PLUGIN" checkout -q -- file.txt

g -C "$PLUGIN" tag v1.2.2
version
check $([[ $RC == 0 && $VERSION == v1.2.2 ]] && resource "$RC_DLL" v1.2.2 1,2,2,0 0x0; echo $?) \
  "a lightweight tag (v1.2.2) on HEAD: a release, 1,2,2,0"

g -C "$PLUGIN" tag -a v1.2.3 -m v1.2.3
version
check $([[ $RC == 0 && $VERSION == v1.2.3 ]]; echo $?) \
  "an annotated tag (v1.2.3) beside a lightweight one on HEAD: the annotated one"
version
check $([[ $RC == 0 && $VERSION == v1.2.3 && $TIME_OF == "$TIME" ]] && resource "$RC_DLL" v1.2.3 1,2,3,0 0x0; echo $?) \
  "a release (tag v1.2.3): 'v1.2.3', 1,2,3,0, no flags"

echo changed > "$PLUGIN/file.txt"
version
check $([[ $RC == 0 && $VERSION == "v1.2.3-$S1-dirty" ]] && resource "$RC_DLL" "v1.2.3-$S1-dirty" 1,2,3,0 0x8; echo $?) \
  "the tag's commit with uncommitted changes: 'v1.2.3-$S1-dirty', 1,2,3,0, PRIVATEBUILD"
g -C "$PLUGIN" checkout -q -- file.txt

echo two > "$PLUGIN/file.txt"; g -C "$PLUGIN" commit -q -am two
echo three > "$PLUGIN/file.txt"; g -C "$PLUGIN" commit -q -am three
C3=$(g -C "$PLUGIN" rev-parse HEAD)
S3=${C3:0:7}
version
check $([[ $RC == 0 && $VERSION == "v1.2.3-$S3" && $COMMIT == "$C3" ]] && resource "$RC_DLL" "v1.2.3-$S3" 1,2,3,2 0x8
        echo $?) "two commits after v1.2.3: 'v1.2.3-$S3', 1,2,3,2, PRIVATEBUILD"

g -C "$PLUGIN" tag -a v1.3.0-beta.1 -m v1.3.0-beta.1
version
check $([[ $RC == 0 && $VERSION == v1.3.0-beta.1 ]] && resource "$RC_DLL" v1.3.0-beta.1 1,3,0,0 0x2; echo $?) \
  "a pre-release (tag v1.3.0-beta.1): 1,3,0,0, PRERELEASE"

echo four > "$PLUGIN/file.txt"; g -C "$PLUGIN" commit -q -am four
C4=$(g -C "$PLUGIN" rev-parse HEAD)
version
check $([[ $RC == 0 && $VERSION == "v1.3.0-beta.1-${C4:0:7}" ]] \
          && resource "$RC_DLL" "v1.3.0-beta.1-${C4:0:7}" 1,3,0,1 0xa; echo $?) \
  "a commit after the pre-release: 1,3,0,1, PRERELEASE and PRIVATEBUILD"

PRODUCT=$'a "quoted" na\xc5\x82me' version
check $(has "$RC_DLL" '      VALUE "ProductName", "a ""quoted"" na??me"'; echo $?) \
  "a text of the resource: a quote doubled, a byte outside printable ASCII as '?'"

mkdir "$RUN/no-git"
version "$RUN/no-git"
check $([[ $RC == 0 && $VERSION == dev && $COMMIT == unknown && $TIME_OF == unknown ]] \
          && resource "$RC_DLL" dev 0,0,0,0 0x8; echo $?) "outside git: 'dev', 'unknown', 'unknown', 0,0,0,0, PRIVATEBUILD"

SOURCE_DATE_EPOCH=1764025925 version "$RUN/no-git"
check $([[ $RC == 0 && $TIME_OF == "$TIME" ]]; echo $?) "outside git with SOURCE_DATE_EPOCH: its time"

mkdir "$RUN/header-only"
RC=0
cmake -DSRC="$PLUGIN" -DOUT="$RUN/header-only/h.h" -DNAME=PLUGIN_VERSION -P "$VERSION_CMAKE" > "$RUN/out.txt" 2>&1 || RC=$?
check $([[ $RC == 0 && $(ls "$RUN/header-only") == h.h ]]; echo $?) "without RC_DIR: the header only"

# the resource linked into the build's test_exports.dll, as the build's header has the version
if [[ -n $BUILD ]]; then
  BUILD_HEADER=$BUILD/gen/test_exports_version.h
  DLL=$BUILD/test_exports.dll
  built=$(sed -n 's/^#define EDPLUGIN_COMMON_TEST_VERSION "\(.*\)"$/\1/p' "$BUILD_HEADER" 2> /dev/null || true)
  READOBJ=$(command -v llvm-readobj || true)
  check $([[ -n $built && -n $READOBJ && -f $DLL ]] \
            && "$READOBJ" --coff-resources "$DLL" | grep -q 'Type: VERSIONINFO (ID 16)' \
            && (($(strings -el "$DLL" | grep -cxF -- "$built") >= 2)) \
            && strings -el "$DLL" | grep -qxF test_exports.dll; echo $?) \
    "test_exports.dll of $BUILD: a VERSIONINFO with '$built' (FileVersion, ProductVersion) and its file name"
fi

echo "=== version_test: $([[ $failures == 0 ]] && echo PASSED || echo FAILED) ($failures failure(s), run directory $RUN)"
[[ $failures == 0 ]]
