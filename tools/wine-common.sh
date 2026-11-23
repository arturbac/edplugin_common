# The common part of the plugins' tools/test.sh, sourced (". <edplugin_common>/tools/wine-common.sh"): the wine
# prefix, EGL without an X server, a new run directory, and every wine run under a time limit.
#   wine_common_prefix <variable>  exports WINEPREFIX from the named variable (a wine prefix kept between runs);
#       fails when it is unset; warns when the prefix does not exist yet (wine makes it on the first run, slowly);
#       with no DISPLAY, WINE_USE_EGL=1 unless set
#   wine_common_run_dir <scratch dir> <name>  prints a new, empty directory <scratch dir>/<name>.XXXXXX
#   wine_common_run <dir> <log> [VAR=value ...] -- <program> [arguments]  runs the program under wine in <dir>, its
#       output appended to <log> (created when missing), limited to WINE_COMMON_TIMEOUT seconds (600 unless set;
#       then SIGKILL 10 s later); returns the program's exit code, 124 when the limit stopped it, 137 when SIGKILL
#       did, 125-127 when timeout or wine could not run, 2 for a word before -- that is not VAR=value
#   wine_common_win_path <path>  prints the path, made absolute, as wine sees it through its Z: drive

wine_common_prefix()
  {
  local var=${1:?variable name}
  [[ -n ${!var:-} ]] || { echo "$0: set $var (a wine prefix kept between runs)" >&2; return 1; }
  export WINEPREFIX=${!var}
  [[ -f $WINEPREFIX/system.reg ]] || echo "$0: no wine prefix in $WINEPREFIX yet; the first run makes it" >&2
  # with no X server (a Wayland-only session) wine's OpenGL, which wined3d needs, comes only through EGL, off unless
  # WINE_USE_EGL is set
  [[ -n ${DISPLAY:-} ]] || export WINE_USE_EGL=${WINE_USE_EGL:-1}
  }

wine_common_run_dir()
  {
  mktemp -d "${1:?scratch dir}/${2:?name}.XXXXXX"
  }

wine_common_run()
  {
  local dir=${1:?run dir} log=${2:?log file}
  shift 2
  [[ $log == /* ]] || log=$PWD/$log
  local -a vars=()
  while [[ $# -gt 0 && $1 != -- ]]; do
    [[ $1 =~ ^[A-Za-z_][A-Za-z0-9_]*= ]] || { echo "wine_common_run: '$1' before -- is not VAR=value" >&2; return 2; }
    vars+=("$1")
    shift
  done
  [[ ${1:-} == -- ]] || { echo "wine_common_run: no -- before the program" >&2; return 2; }
  shift
  : >> "$log"
  (cd "$dir" && env WINEDEBUG=-all "${vars[@]}" timeout -k 10 "${WINE_COMMON_TIMEOUT:-600}" wine "$@" >> "$log" 2>&1)
  }

wine_common_win_path()
  {
  local path
  path=$(realpath -m "${1:?path}")
  echo "Z:${path//\//\\}"
  }
