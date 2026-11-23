# The common part of the plugins' tools/test.sh, sourced (". <edplugin_common>/tools/wine-common.sh"): the wine
# prefix, EGL without an X server, a new run directory, and every wine run under a time limit.
#   wine_common_prefix <variable>  exports WINEPREFIX from the named variable (a wine prefix kept between runs);
#       fails when it is unset; makes the prefix by wineboot when it has none yet (minutes, once, under the same
#       time limit as a run; a warning when it fails); starts one wineserver kept 600 s after its last client, for
#       all the runs and the next test; with no DISPLAY, WINE_USE_EGL=1 unless set
#   wine_common_run_dir <scratch dir> <name>  prints a new, empty directory <scratch dir>/<name>.XXXXXX, an absolute
#       path also for a relative scratch dir
#   wine_common_exec [VAR=value ...] -- <program> [arguments]  runs the program under wine in the current directory
#       with WINEDEBUG=-all (a VAR=value may set it otherwise), no input (/dev/null), its output left to the caller,
#       limited to WINE_COMMON_TIMEOUT seconds (600 unless set; then SIGKILL 10 s later); returns the program's exit
#       code, 124 when the limit stopped it, 137 when SIGKILL did (both said on stderr), 125-127 when timeout or wine
#       could not run, 2 for a word before -- that is not VAR=value
#   wine_common_run <dir> <log> [VAR=value ...] -- <program> [arguments]  wine_common_exec in <dir>, its output
#       appended to <log> (created when missing); the same exit codes
#   wine_common_win_path <path>  prints the path, made absolute (links not followed), as wine sees it through its
#       Z: drive

wine_common_prefix()
  {
  local var=${1:?variable name}
  [[ -n ${!var:-} ]] || { echo "$0: set $var (a wine prefix kept between runs)" >&2; return 1; }
  export WINEPREFIX=${!var}
  # with no X server (a Wayland-only session) wine's OpenGL, which wined3d needs, comes only through EGL, off unless
  # WINE_USE_EGL is set
  [[ -n ${DISPLAY:-} ]] || export WINE_USE_EGL=${WINE_USE_EGL:-1}
  if [[ ! -f $WINEPREFIX/system.reg ]]; then
    echo "$0: no wine prefix in $WINEPREFIX yet; wineboot makes it (minutes, once)" >&2
    mkdir -p "$WINEPREFIX"
    WINEDEBUG=-all timeout -k 10 "${WINE_COMMON_TIMEOUT:-600}" wineboot -i > /dev/null 2>&1 < /dev/null \
      || echo "$0: wineboot ended with code $? in $WINEPREFIX; the prefix may be incomplete" >&2
  fi
  wineserver -p600 > /dev/null 2>&1 || true  # already running (e.g. from wineboot) is fine
  }

wine_common_run_dir()
  {
  mktemp -d "$(realpath -m "${1:?scratch dir}")/${2:?name}.XXXXXX"
  }

wine_common_exec()
  {
  local -a vars=()
  while [[ $# -gt 0 && $1 != -- ]]; do
    [[ $1 =~ ^[A-Za-z_][A-Za-z0-9_]*= ]] || { echo "wine_common_exec: '$1' before -- is not VAR=value" >&2; return 2; }
    vars+=("$1")
    shift
  done
  [[ ${1:-} == -- ]] || { echo "wine_common_exec: no -- before the program" >&2; return 2; }
  shift
  local rc=0
  # no input: a program in the background group of a terminal (timeout's own) would stop at reading it
  env WINEDEBUG=-all "${vars[@]}" timeout -k 10 "${WINE_COMMON_TIMEOUT:-600}" wine "$@" < /dev/null || rc=$?
  ((rc != 124 && rc != 137)) || echo "wine_common_exec: $1 stopped at the time limit (${WINE_COMMON_TIMEOUT:-600} s)" >&2
  return $rc
  }

wine_common_run()
  {
  local dir=${1:?run dir} log=${2:?log file}
  shift 2
  [[ $log == /* ]] || log=$PWD/$log
  : >> "$log"
  (cd "$dir" && wine_common_exec "$@" >> "$log" 2>&1)
  }

wine_common_win_path()
  {
  local path
  path=$(realpath -ms "${1:?path}")  # -s: a link stays the path given, not its target
  echo "Z:${path//\//\\}"
  }
