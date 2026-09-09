#!/usr/bin/env bash
#
# LVGL-Host-Simulator: zeigt das UI aus main/weather_ui.c in einem SDL-Fenster
# auf dem Desktop. Maus = Touch, PC-Tastatur schreibt in die Textfelder.
#
# Exit 0   = Fenster wurde vom Benutzer geschlossen
# Exit !=0 = Build- oder Startfehler
# Exit 127 = SDL2-Entwicklungspakete fehlen
#
# Braucht weder ESP-IDF noch Hardware noch Emulator. Zeigt NICHT: DSI-Timing,
# PPA-Rotation, Tear-Avoid-Modus, echtes Panelfarbverhalten.
#
# Argumente werden an das Programm durchgereicht, z.B.:
#   ./scripts/sim.sh --wifi-setup

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="$REPO_ROOT/sim"
BUILD_DIR="$PROJECT_DIR/build-sim"
LOG_DIR="$REPO_ROOT/.logs"
LOG="$LOG_DIR/sim.log"

mkdir -p "$LOG_DIR"

# --- Voraussetzungen --------------------------------------------------------
if ! pkg-config --exists sdl2 2>/dev/null; then
    echo "FEHLER: SDL2-Entwicklungsdateien nicht gefunden." >&2
    echo "Installation: sudo apt install libsdl2-dev" >&2
    exit 127
fi

for tool in cmake gcc; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "FEHLER: $tool fehlt." >&2
        exit 127
    fi
done

# Der Simulator baut fuer den Rechner, nicht fuer den Chip. Steht eine
# Cross-Toolchain vor /usr/bin auf dem PATH, uebersetzt gcc zwar noch, scheitert
# aber beim Assemblieren mit "Unbekannte Option --64" -- gcc reicht die
# x86-64-Flagge an ein 'as' weiter, das nur RISC-V oder Xtensa kennt. Die
# ESP-Toolchains bringen unter <toolchain>/<target>/bin/ ein unpraefigiertes
# 'as' mit, das genau das ausloest, sobald dieses Verzeichnis auf den PATH
# geraet. Einmal probeweise uebersetzen sagt das klarer als binutils es tut.
probe="$(mktemp -d)"
printf 'int main(void){return 0;}\n' > "$probe/probe.c"
if ! gcc -o "$probe/probe" "$probe/probe.c" >"$probe/err" 2>&1; then
    echo "FEHLER: gcc kann auf diesem Rechner nichts uebersetzen." >&2
    echo "--- Meldung ---" >&2
    cat "$probe/err" >&2
    echo "--- Werkzeuge ---" >&2
    echo "gcc: $(command -v gcc)" >&2
    echo "as : $(command -v as)   (muss /usr/bin/as sein, nicht eine Cross-Toolchain)" >&2
    echo >&2
    echo "Meist steht eine ESP-Toolchain vor /usr/bin auf dem PATH." >&2
    echo "Pruefen mit:  which -a as gcc" >&2
    echo "Umgehen mit:  env -i HOME=\"$HOME\" PATH=/usr/bin:/bin DISPLAY=\"$DISPLAY\" \\" >&2
    echo "                  WAYLAND_DISPLAY=\"${WAYLAND_DISPLAY:-}\" XDG_RUNTIME_DIR=\"${XDG_RUNTIME_DIR:-}\" \\" >&2
    echo "                  $0 $*" >&2
    rm -rf "$probe"
    exit 127
fi
rm -rf "$probe"

# --- Build ------------------------------------------------------------------
echo "== Build =="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja >>"$LOG" 2>&1 \
    || { echo "SIM_BUILD_FAILED (cmake configure). Volles Log: $LOG" >&2; tail -n 20 "$LOG" >&2; exit 1; }
cmake --build "$BUILD_DIR" >>"$LOG" 2>&1 \
    || { echo "SIM_BUILD_FAILED (compile). Volles Log: $LOG" >&2; tail -n 20 "$LOG" >&2; exit 1; }

# --- Sammelaufnahme ---------------------------------------------------------
# --shots ZIELVERZEICHNIS nimmt jeden Screen einzeln auf. Das ist der Satz, den
# man Claude Design in die Hand druecken kann: ein Bild pro Zustand statt einer
# Beschreibung. Laeuft headless, braucht also keine Grafiksitzung.
if [ "${1:-}" = "--shots" ]; then
    OUT="${2:-}"
    if [ -z "$OUT" ]; then
        echo "FEHLER: --shots braucht ein Zielverzeichnis." >&2
        exit 2
    fi
    mkdir -p "$OUT"

    echo "== Aufnahmen nach $OUT =="
    for screen in main detail search settings settings-adaptive-on wifi; do
        SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
            "$BUILD_DIR/weather_sim" --screen "$screen" --screenshot "$OUT/$screen.bmp" \
            || { echo "FEHLER bei Screen '$screen'." >&2; exit 1; }
        echo "  $screen.bmp"
    done
    # Erstboot ist ein eigener Zustand, kein eigener Screen: offline, ohne Daten.
    SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
        "$BUILD_DIR/weather_sim" --wifi-setup --screenshot "$OUT/first-boot.bmp" \
        || { echo "FEHLER bei Screen 'first-boot'." >&2; exit 1; }
    echo "  first-boot.bmp"

    # SDL kann nur BMP schreiben. PNG ist zum Weitergeben handlicher, also
    # umwandeln, wo Pillow da ist — und sonst die BMPs einfach stehen lassen.
    if python3 -c "import PIL" >/dev/null 2>&1; then
        python3 - "$OUT" <<'PYCONV'
import pathlib, sys
from PIL import Image
for bmp in sorted(pathlib.Path(sys.argv[1]).glob("*.bmp")):
    Image.open(bmp).convert("RGB").save(bmp.with_suffix(".png"))
    bmp.unlink()
PYCONV
        echo "== Fertig: $(ls "$OUT"/*.png | wc -l) PNG in $OUT =="
    else
        echo "== Fertig: BMP in $OUT (Pillow fehlt, keine PNG-Umwandlung) =="
    fi
    exit 0
fi

# --- Start ------------------------------------------------------------------
if [[ " $* " == *" --screenshot "* ]]; then
    echo "== Simulator: Screenshot, beendet sich selbst =="
else
    echo "== Simulator: Fenster schliessen zum Beenden =="
fi
exec "$BUILD_DIR/weather_sim" "$@"
