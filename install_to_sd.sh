#!/usr/bin/env bash
set -euo pipefail

# Installs Marine Aquarium 2 (software-rendered port) on an SF2000 / GB300 SD card.
# The original game data (data1.hdr + data1.cab) must be present in ./assets or
# already installed on the card.
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
DIST="$PROJECT_DIR/dist"
SD_ROOT="${1:-}"

if [ -z "$SD_ROOT" ]; then
    for p in /media/Sajnaps/GB300 /media/Sajnaps/SF2000 /media/* /mnt/*; do
        if [ -d "$p/system/Deimos/cores" ] || [ -d "$p/cores" ]; then
            SD_ROOT="$p"
            break
        fi
    done
fi

if [ -z "$SD_ROOT" ] || [ ! -d "$SD_ROOT" ]; then
    echo "Nie wykryto karty SD. Uzycie: $0 <sciezka_do_karty>"
    exit 1
fi

IS_GB300=0
if [ "${FORCE_GB300:-0}" = "1" ] || [[ "$SD_ROOT" =~ GB300 ]] || \
   [ -f "$SD_ROOT/cores/sonicmania.gb3k" ] || \
   [ -f "$SD_ROOT/system/Deimos/cores/sonicmania.gb3k" ]; then
    IS_GB300=1
fi

if [ "$IS_GB300" -eq 1 ]; then
    CORE="$DIST/aquarium_GB300V2.gb3k"
    echo "Wykryto GB300 / GB300V2"
else
    CORE="$DIST/aquarium_SF2000.sf2k"
    echo "Wykryto Data Frog SF2000"
fi

if [ ! -f "$CORE" ]; then
    echo "Brak $CORE - uruchom najpierw: make device FROGGY_TYPE=..."
    exit 1
fi

echo "Instalacja Marine Aquarium 2 w: $SD_ROOT"

mkdir -p "$SD_ROOT/system/Deimos/cores" "$SD_ROOT/cores/aquarium" \
         "$SD_ROOT/ROMS/aquarium/.res" "$SD_ROOT/ROMS/.res" \
         "$SD_ROOT/configs/custom_cores/aquarium" "$SD_ROOT/bios"

# The loader always opens "<core>.sf2k"; keep both names for GB300/SF2000.
for ext in sf2k gb3k; do
    cp -f "$CORE" "$SD_ROOT/system/Deimos/cores/aquarium.$ext"
    cp -f "$CORE" "$SD_ROOT/cores/aquarium.$ext"
    cp -f "$CORE" "$SD_ROOT/ROMS/aquarium/aquarium.$ext"
done
cp -f "$CORE" "$SD_ROOT/cores/aquarium/core_87000000"
cp -f "$CORE" "$SD_ROOT/ROMS/aquarium/core_87000000"

# Game data. Keep the original CD cabinet files next to the stub so the core can
# extract "Marine Aquarium 2.scr" at startup.
if [ ! -f "$SD_ROOT/ROMS/aquarium/data1.cab" ]; then
    if [ -f "$PROJECT_DIR/assets/data1.cab" ] && [ -f "$PROJECT_DIR/assets/data1.hdr" ]; then
        cp -f "$PROJECT_DIR/assets/data1.hdr" "$PROJECT_DIR/assets/data1.cab" "$SD_ROOT/ROMS/aquarium/"
        echo "Skopiowano dane z assets/ (data1.hdr, data1.cab)"
    else
        echo "UWAGA: brak data1.hdr/data1.cab w assets."
        echo "       Skopiuj je z plyty Marine Aquarium 2003 do $SD_ROOT/ROMS/aquarium/"
    fi
fi
if [ ! -f "$SD_ROOT/ROMS/aquarium/aquarium.ini" ]; then
    cp -f "$PROJECT_DIR/assets/aquarium.ini" "$SD_ROOT/ROMS/aquarium/aquarium.ini"
fi

printf 'aquarium;aquarium;Aquarium.gba' > "$SD_ROOT/ROMS/aquarium/Aquarium.gba"
printf 'aquarium;aquarium;Aquarium.gba' > "$SD_ROOT/ROMS/Aquarium.gba"
printf 'Aquarium.gba|aquarium\nAquarium|aquarium\n' > "$SD_ROOT/configs/custom_cores/aquarium/games.opt"

if [ -f "$DIST/res/Aquarium.png" ]; then
    cp -f "$DIST/res/Aquarium.png" "$SD_ROOT/ROMS/aquarium/.res/Aquarium.png"
    cp -f "$DIST/res/Aquarium.png" "$SD_ROOT/ROMS/aquarium/Aquarium.png"
fi
if [ -f "$DIST/res/Aquarium.rgb565" ]; then
    cp -f "$DIST/res/Aquarium.rgb565" "$SD_ROOT/ROMS/aquarium/.res/Aquarium.rgb565"
    cp -f "$DIST/res/Aquarium.rgb565" "$SD_ROOT/ROMS/aquarium/Aquarium.rgb565"
    cp -f "$DIST/res/Aquarium.rgb565" "$SD_ROOT/ROMS/.res/Aquarium.rgb565"
    cp -f "$DIST/res/Aquarium.rgb565" "$SD_ROOT/ROMS/Aquarium.rgb565"
fi

# Boot log helps if the first run needs debugging.
: > "$SD_ROOT/bios/enable_xlog.txt"

sync
echo "=========================================================="
echo " Marine Aquarium 2 zainstalowany."
echo " Uruchom z menu: ROMS/aquarium/Aquarium.gba"
echo " Log startu:     /mnt/sda1/aquarium.log + system/logs/Multicore.log"
echo "=========================================================="
