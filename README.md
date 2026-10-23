# Marine Aquarium 2.0 for the R36S
---
Ported from the original 2000's era screensaver that was included with a Windows PC purchase. All necessary files are included.

<p align=center>
<img width="640" height="179" alt="setup" src="https://github.com/user-attachments/assets/95d036ac-79bd-42db-b3d9-92d94d20d517" />
<img width="640" height="480" alt="final" src="https://github.com/user-attachments/assets/20ac929a-bb22-4394-9683-010e67c5601a" />
</p>

## Installation
Place the launcher and folder in Tools. Launch with Aquarium.sh.

## Requirements
SDL2 installed on the device.

## Settings
Aquarium options may be set by editing the launcher.
```
# ---------------- settings ----------------
FISH_COUNT=7            # 1-8
ALL_RANDOM=0            # 1 = every fish random (FISH1-FISH8 ignored)

# FISH1-FISH8: random, off, or one of:
#   Yellow_Tang  Golden_Butterflyfish  Powder_Blue_Tang  Flame_Angel  Queen_Angel
#   Royal_Gramma  Copperband_Butterflyfish  Addis_Butterflyfish  French_Angel
#   Wimplefish  Forceps_Fish  Regal_Tang  Clown_Triggerfish  Percula_Clownfish
#   Purple_Tang  Picasso_Triggerfish  Regal_Angel  Blue_Damselfish  Koran_Angel
#   Three_Stripe_Damselfish  LionFish  EmperorAngel  BlueRingAngel  RedfinBfly
#   RockBeauty  Squarespot
FISH1=random
FISH2=random
FISH3=random
FISH4=random
FISH5=random
FISH6=random
FISH7=random
FISH8=random

SOUND=1                 # 0/1
BUBBLE_COLUMN=1         # 0/1
AUTO_LIGHTS=1           # 1 = day/night cycle (FOREGROUND/BACKGROUND_LIGHTS ignored)
FOREGROUND_LIGHTS=100   # 0-100
BACKGROUND_LIGHTS=100   # 0-100
STARFISH_ON_GLASS=1     # 1 = starfish may climb the front glass
# ------------------------------------------
```

## Gamepad Controls

| Key | Action |
| --- | --- |
| D-Pad Left or Right | Background lights up or down |
| D-Pad Up or Down | Foreground Lights Up or Down |
| A Button | Bubbles On/Off |
| Select or Start | Exit |
