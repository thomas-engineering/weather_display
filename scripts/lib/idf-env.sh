# ESP-IDF-Umgebung fuer die Projektskripte. Wird gesourct, nicht ausgefuehrt:
#
#   source "$REPO_ROOT/scripts/lib/idf-env.sh"
#
# Ist idf.py schon im PATH, passiert nichts. Sonst, in dieser Reihenfolge:
#   1. $IDF_ACTIVATE (Standard: ~/.espressif/tools/activate_idf_v6.1.sh) —
#      das eim-Aktivierungsskript waehlt die zur IDF-Version passende venv.
#   2. $IDF_PATH/export.sh (Standard: ~/esp/esp-idf) fuer klassische Installs.
# Ist idf.py danach immer noch nicht da: Exit 127.

if ! command -v idf.py >/dev/null 2>&1; then
    _idf_activate="${IDF_ACTIVATE:-$HOME/.espressif/tools/activate_idf_v6.1.sh}"
    _idf_export="${IDF_PATH:-$HOME/esp/esp-idf}/export.sh"
    if [ -f "$_idf_activate" ]; then
        # The eim activation script decides "sourced or executed" by looking at
        # $0 and exits unless $0 is a shell name — which it never is inside a
        # script. Source it in a child whose $0 is "bash" and import its env.
        while IFS= read -r -d '' _idf_kv; do
            case "${_idf_kv%%=*}" in
                PWD|OLDPWD|SHLVL|_) ;;
                *) export "$_idf_kv" ;;
            esac
        done < <(bash -c 'source "$1" >/dev/null 2>&1; env -0' bash "$_idf_activate")
        # eim provides idf.py as a shell function, which env cannot carry over.
        if [ -n "${IDF_PATH:-}" ] && [ -n "${IDF_PYTHON_ENV_PATH:-}" ]; then
            idf.py() { "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@"; }
            export -f idf.py
        fi
    elif [ -f "$_idf_export" ]; then
        # export.sh is not written for set -eu; relax it while sourcing.
        _idf_shell_opts="$(set +o)"
        set +eu
        # shellcheck disable=SC1090
        source "$_idf_export" >/dev/null 2>&1
        eval "$_idf_shell_opts"
    fi
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "FEHLER: ESP-IDF nicht gefunden (IDF_ACTIVATE=$_idf_activate, IDF_PATH-export=$_idf_export)" >&2
        exit 127
    fi
    unset _idf_activate _idf_export _idf_kv _idf_shell_opts
fi
