#!/usr/bin/env bash
# fake_ed's runs under wine, the plugins' chain without the game: fake_ed.exe -> edloader -> the run's list -> the
# system d3d11, each run in a new directory under the scratch dir (game/: fake_ed.exe, edloader as d3d11.dll; root/:
# EDLOADER_DIR with edloader.txt, plugins/, config/, logs/):
#   r1   probe_plugin.dll alone
#   r2a  EDHM (3Dmigoto) before probe_plugin.dll
#   r2b  probe_plugin.dll before EDHM
# r2a and r2b only with EDHM_DIR. A run's RUN.txt has the sha256 of every file put in it before the run (and, with
# EDHM, how its d3dx.ini differs from EDHM_DIR's), fake_ed's exit code, the wine version and the library's commit;
# after the run the files it made are listed there. The logs stay where they were written (game/: fake_ed's;
# root/logs/: edloader's and the probe's; EDHM's, if any, beside its dll or the exe). EDHM's d3dx.ini is copied with
# its [Loader] target set to fake_ed.exe (3Dmigoto does not start in an exe other than its target).
# A run passes when fake_ed ran every phase, edloader's chain is the list's, made the device and no element of it
# stopped calling on, the probe attached its three plugins, reported, saw 3Dmigoto before it (r2a) or 3Dmigoto's
# wrapper as the context (r2b), no hook called itself again, and each plugin's hooks worked exactly the Draw and
# IASetPrimitiveTopology calls of fake_ed's phases and at least their ClearState and Flush calls (one more of each
# after the phases). EDHM missing from the chain, the chain stopped, or EDHM not calling on is said on a line of its
# own with edloader's lines that name EDHM.
# The runs use the host's GPU (wined3d) and show fake_ed's window, about 17 s each.
# Environment: EDPLUGIN_COMMON_WINEPREFIX (a wine prefix kept between runs), EDLOADER_DLL (edloader's d3d11.dll),
# EDHM_DIR (a copy of EDHM's files without links: the dll named by EDHM_DLL, d3d11_edhm.dll unless set,
# d3dcompiler_47.dll, d3dx.ini, EDHM-ini/, ShaderFixes/), WINE_COMMON_TIMEOUT (seconds for one wine run, 120 unless
# set). Needs the build's fake_ed.exe and probe_plugin.dll (EDPLUGIN_COMMON_FAKE_ED, EDPLUGIN_COMMON_PROBE_PLUGIN).
# Usage: tools/fake_ed/test.sh <build dir> <scratch dir>
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
. "$HERE/wine-common.sh"
BUILD=$(cd "${1:?build dir}" && pwd)
SCR=${2:?scratch dir}
[[ -n ${EDLOADER_DLL:-} ]] || { echo "$0: set EDLOADER_DLL (edloader's d3d11.dll)" >&2; exit 1; }
for f in "$BUILD/fake_ed.exe" "$BUILD/probe_plugin.dll" "$EDLOADER_DLL"; do
  [[ -f $f ]] || { echo "$0: no $f (build first)" >&2; exit 1; }
done
EDHM_DLL=${EDHM_DLL:-d3d11_edhm.dll}
runs=(r1)
if [[ -n ${EDHM_DIR:-} ]]; then
  for f in "$EDHM_DLL" d3dcompiler_47.dll d3dx.ini EDHM-ini ShaderFixes; do
    [[ -e $EDHM_DIR/$f ]] || { echo "$0: no $f in EDHM_DIR ($EDHM_DIR)" >&2; exit 1; }
  done
  # a link (EDHM-UI's folders under Linux) would let 3Dmigoto write outside the run's directory
  [[ -z $(find "$EDHM_DIR" -type l -print -quit) ]] || { echo "$0: links in EDHM_DIR ($EDHM_DIR); a copy without them" >&2; exit 1; }
  runs+=(r2a r2b)
fi
wine_common_prefix EDPLUGIN_COMMON_WINEPREFIX  # a new prefix's wineboot under its own limit, 600 s unless set
export WINE_COMMON_TIMEOUT=${WINE_COMMON_TIMEOUT:-120}
wine_version=$(wine --version 2> /dev/null || echo "not told")
commit=$(git -C "$HERE/.." describe --always --dirty 2> /dev/null || echo "not told")
run=
failed=0
fail() { echo "FAIL $run: $*"; failed=1; }
# sum_of <file> <text> <name> [<part>]: the sum of the numbers after the word <name> on the lines of <file> that have
# <text>; of a number like 63/0 its <part> (1 unless given: the probe's worked, 2 its passed)
sum_of()
  {
  awk -v text="$2" -v name="$3" -v part="${4:-1}" 'index($0, text) {
      for(i = 1; i < NF; ++i) if($i == name && $(i + 1) ~ /^[0-9]/) { split($(i + 1), n, "/"); s += n[part] } }
    END { print s + 0 }' "$1"
  }
for run in "${runs[@]}"; do
  dir=$(wine_common_run_dir "$SCR" "fake_ed-$run")
  game=$dir/game root=$dir/root
  mkdir -p "$game" "$root/plugins"
  cp "$BUILD/fake_ed.exe" "$game/"
  cp "$EDLOADER_DLL" "$game/d3d11.dll"
  cp "$BUILD/probe_plugin.dll" "$root/plugins/"
  case $run in
    r1) list=(probe_plugin.dll) ;;
    r2a) list=(+d3dcompiler_47.dll "$EDHM_DLL" probe_plugin.dll) ;;
    r2b) list=(+d3dcompiler_47.dll probe_plugin.dll "$EDHM_DLL") ;;
  esac
  if [[ $run != r1 ]]; then
    cp "$EDHM_DIR/$EDHM_DLL" "$EDHM_DIR/d3dcompiler_47.dll" "$root/plugins/"
    # 3Dmigoto refuses to start (DllMain fails, edloader's error 1114) in an exe other than its [Loader] target, the
    # game's: the run's copy of d3dx.ini names fake_ed.exe there, the one line changed
    awk '/^[ \t]*\[/ { loader = tolower($0) ~ /^[ \t]*\[loader\]/ }
      loader && tolower($0) ~ /^[ \t]*target[ \t]*=/ { cr = /\r$/ ? "\r" : ""; $0 = "target = fake_ed.exe" cr; n++ }
      { print } END { exit n == 1 ? 0 : 1 }' "$EDHM_DIR/d3dx.ini" > "$root/plugins/d3dx.ini" \
      || { echo "$0: not one target line in d3dx.ini's [Loader]" >&2; exit 1; }
    ini_sum=$(sha256sum < "$EDHM_DIR/d3dx.ini")
    ini_sum=${ini_sum%% *}
    ini_diff=$(diff "$EDHM_DIR/d3dx.ini" "$root/plugins/d3dx.ini" || true)
    # EDHM-ini and ShaderFixes beside the game (EDHM's installer) and beside d3dx.ini (an installation into edloader's
    # plugins), as players have them
    cp -r "$EDHM_DIR/EDHM-ini" "$EDHM_DIR/ShaderFixes" "$game/"
    cp -r "$EDHM_DIR/EDHM-ini" "$EDHM_DIR/ShaderFixes" "$root/plugins/"
  fi
  { printf '# fake_ed %s\r\n' "$run"; printf '%s\r\n' "${list[@]}"; } > "$root/edloader.txt"
  { echo "wine: $wine_version; edplugin_common: $commit"
    [[ $run == r1 ]] || printf 'd3dx.ini: the run'"'"'s copy differs from EDHM_DIR'"'"'s (%s) in:\n%s\n' "$ini_sum" "$ini_diff"
    (cd "$dir" && find game root -type f -print0 | sort -z | xargs -0 sha256sum); } > "$dir/RUN.txt"
  code=0
  wine_common_run "$game" "$dir/console.txt" EDLOADER_DIR="$(wine_common_win_path "$root")" \
    WINEDLLOVERRIDES="d3d11=n,b" EDLOADER_WINDOW=test -- fake_ed.exe || code=$?
  made_files=$(cd "$dir" && find game root -type f -newer RUN.txt | sort)
  printf 'fake_ed: exit code %s\nfiles the run made:\n%s\n' "$code" "$made_files" >> "$dir/RUN.txt"
  echo "--- $run: $dir, fake_ed exit code $code"
  fake_log=$(find "$game" -maxdepth 1 -name 'fake_ed-*.txt' | sort | head -n 1)
  probe_log=$(find "$root/logs" -maxdepth 1 -name 'probe_plugin-*.txt' 2> /dev/null | sort | head -n 1 || true)
  loader_log=$root/logs/edloader.log
  ((code == 0)) || fail "fake_ed's exit code $code"
  [[ -n $fake_log ]] && grep -q "done: every phase ran" "$fake_log" || fail "fake_ed did not run every phase"
  [[ -z $fake_log ]] || ! grep -q "the protection never changed" "$fake_log" \
    || echo "$run: the multithread protection never changed (no ID3D11Multithread)"
  if [[ ! -f $loader_log ]]; then
    fail "no edloader.log"
  else
    chained=()
    for line in "${list[@]}"; do [[ $line == +* ]] || chained+=("$line"); done
    for i in "${!chained[@]}"; do
      grep -qF "chain[$i] = ${chained[i]}" "$loader_log" || fail "chain[$i] is not ${chained[i]}"
    done
    grep -qF "through ${#chained[@]} element(s), device made" "$loader_log" || fail "the device not made through the chain"
    ! grep -qF "did not call on" "$loader_log" || fail "an element of the chain did not call on"
    if [[ $run != r1 ]]; then
      why=
      grep -qF "] = $EDHM_DLL" "$loader_log" || why="on no chain line"
      ! grep -qF "edloader stopped by" "$loader_log" || why="edloader stopped"
      ! grep -qF "$EDHM_DLL did not call on" "$loader_log" || why="it did not call on"
      if [[ -n $why ]]; then
        echo "EDHM NOT IN THE CHAIN ($run): $EDHM_DLL $why; edloader's lines that name it:"
        grep -F "$EDHM_DLL" "$loader_log" || echo "  (none)"
        fail "EDHM not in the chain"
      fi
    fi
  fi
  if [[ -z $probe_log ]]; then
    fail "no probe_plugin-*.txt in root/logs"
  else
    grep -q "report: every second from now" "$probe_log" || fail "the probe did not report"
    if [[ $run == r2a ]]; then
      grep -q "chain: before probe_plugin: .* - 3Dmigoto" "$probe_log" || fail "the probe did not see 3Dmigoto before it"
    fi
    # 3Dmigoto after the probe wraps the context it gives back
    if [[ $run == r2b ]]; then
      grep -q "device: .*: a wrapper's context" "$probe_log" || fail "the probe's context is not a wrapper's"
    fi
    ! grep -q "a hook called itself again: yes" "$probe_log" || fail "a hook called itself again"
    for p in A B C; do
      grep -qF "plugin $p attach: attached" "$probe_log" || fail "plugin $p not attached"
    done
  fi
  if [[ -n $fake_log && -n $probe_log ]]; then
    for call in Draw IASetPrimitiveTopology ClearState Flush; do
      made=$(sum_of "$fake_log" "phase: " "$call")  # the phases' end lines alone have counts
      for p in A B C; do
        counted=$(sum_of "$probe_log" "plugin $p: Draw " "$call")
        passed=$(sum_of "$probe_log" "plugin $p: Draw " "$call" 2)
        echo "$run $call: fake_ed's phases $made, plugin $p worked $counted, passed $passed"
        case $call in
          Draw | IASetPrimitiveTopology) ((counted == made)) || fail "plugin $p worked $counted $call of $made" ;;
          *) ((counted >= made)) || fail "plugin $p worked $counted $call of $made" ;;
        esac
      done
    done
  fi
done
((failed == 0)) || { echo "fake_ed test FAILED"; exit 1; }
echo "fake_ed test PASSED"
