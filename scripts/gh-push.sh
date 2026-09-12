#!/usr/bin/env bash
#
# Committet ausgewaehlte Dateien und pusht auf den aktuellen Branch (origin).
#
# Nutzung:
#   ./scripts/gh-push.sh -m "Commit-Nachricht" <datei> [<datei> ...]
#   ./scripts/gh-push.sh -m "Commit-Nachricht" --staged   # nutzt bereits Gestagtes
#
# Committet absichtlich NIE mit "git add -A" oder "-u": entweder werden die
# uebergebenen Dateien einzeln gestaged, oder es wird genutzt, was bereits
# mit "git add" vorgemerkt ist (--staged). Bei unklarer Dateiauswahl (nichts
# uebergeben, nichts gestaged, aber Aenderungen vorhanden) bricht das Skript
# mit einer Liste der Kandidaten ab, statt zu raten — das ist der Punkt, an
# dem der Aufrufer (Mensch oder Agent) erst entscheiden bzw. nachfragen muss.
#
# Kein --force, kein --no-verify. Pusht auf origin/<aktueller-branch>.
#
# Exit 0  = committet und gepusht (oder: nichts zu tun, sauberer Baum)
# Exit 1  = Dateiauswahl unklar oder git-Fehler
# Exit 127 = kein Git-Repository

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/gh-push.log"
mkdir -p "$LOG_DIR"

cd "$REPO_ROOT"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "FEHLER: kein Git-Repository unter $REPO_ROOT" >&2
    exit 127
fi

MESSAGE=""
FILES=()
USE_STAGED=0

while [ $# -gt 0 ]; do
    case "$1" in
        -m)
            MESSAGE="${2:-}"
            shift 2
            ;;
        --staged)
            USE_STAGED=1
            shift
            ;;
        *)
            FILES+=("$1")
            shift
            ;;
    esac
done

if [ -z "$MESSAGE" ]; then
    echo "FEHLER: Commit-Nachricht fehlt. Nutzung: $0 -m \"...\" <datei> [<datei> ...]" >&2
    exit 1
fi

{
    echo "== $(date -Iseconds) =="
    git status --porcelain
} >> "$LOG"

if [ "$USE_STAGED" -eq 1 ]; then
    if [ -z "$(git diff --cached --name-only)" ]; then
        echo "FEHLER: --staged verlangt, dass bereits etwas mit 'git add' vorgemerkt ist." >&2
        exit 1
    fi
elif [ ${#FILES[@]} -gt 0 ]; then
    git add -- "${FILES[@]}"
else
    CHANGED="$(git status --porcelain)"
    if [ -z "$CHANGED" ]; then
        echo "Arbeitsverzeichnis sauber, nichts zu committen."
    else
        echo "FEHLER: keine Dateien angegeben und nichts gestaged. Kandidaten:" >&2
        echo "$CHANGED" >&2
        echo "Entweder Dateien explizit angeben oder erst 'git add' und --staged nutzen." >&2
        exit 1
    fi
fi

if [ -z "$(git diff --cached --name-only)" ]; then
    echo "Nichts zu committen."
    exit 0
fi

echo "== Committe folgende Dateien =="
git diff --cached --name-only

git commit -m "$MESSAGE"

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
echo "== Push auf origin/$BRANCH =="
git push origin "$BRANCH" 2>&1 | tee -a "$LOG"

echo "Log: $LOG"
