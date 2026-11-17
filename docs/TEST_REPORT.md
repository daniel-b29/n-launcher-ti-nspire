# Rapport de Compilation et de Validation Technique — N-Launcher v3.1 (`N-Launcher.tns`)

## 1. Environnement de Compilation et SDK Utilisés

| Composant | Version / Révision | Rôle |
| :--- | :--- | :--- |
| **Compilateur croisé ARM** | `arm-none-eabi-gcc` `12.2.1` | Compilation C11 modulaire vers ARMv5TE (`-marm -std=c11 -Os`) |
| **Binutils ARM** | `binutils-arm-none-eabi` `2.40` | Assemblage et édition de liens ELF32 ARM (`arm-none-eabi-ld`) |
| **Bibliothèque C embarquée** | `libnewlib-arm-none-eabi` | Runtime standard C embarqué |
| **Ndless SDK** | `ndless-nspire/Ndless` (Zehn v1) | En-têtes `os.h`, `libndls.h`, `keys.h`, `libsyscalls.a`, `libndls.a` |
| **Générateur Zehn & PRG** | `genzehn` + `make-prg` (`zehn_loader.tns`) | Conversion ELF32 -> Zehn v1 relogeable -> exécutable `PRG\0` |

---

## 2. Vérification des Fichiers Exécutables Générés (v3.1)

### 2.1 Compilation Modulaire Sans Avertissement (`0 warning, 0 error`)
```text
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c nl_boot_stub.c -o nl_boot_stub.o
nspire-ld nl_boot_stub.o -o nl_boot_stub.elf -Wl,--gc-sections
genzehn --input nl_boot_stub.elf --output nl_boot_stub.tns.zehn --name "nl_boot" --author "daniel.b" --version 1 --notice "N-Launcher Autostart Bootstrapper" --ndless-min 36 --240x320-support true
make-prg nl_boot_stub.tns.zehn nl_boot_stub.tns
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c main_ndless.c -o main_ndless.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c scanner.c -o scanner.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c covers.c -o covers.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c storage.c -o storage.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c ui_state.c -o ui_state.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c animation.c -o animation.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -Os -c renderer.c -o renderer.o
nspire-ld main_ndless.o scanner.o covers.o storage.o ui_state.o animation.o renderer.o -o N-Launcher.elf -Wl,--gc-sections
genzehn --input N-Launcher.elf --output N-Launcher.tns.zehn --name "N-Launcher" --author "daniel.b" --version 3 --notice "Console Game Library Launcher for TI-Nspire CX II-T" --ndless-min 36 --240x320-support true
make-prg N-Launcher.tns.zehn N-Launcher.tns
```

### 2.2 Inspection Officielle via `genzehn --info`
```text
Zehn header starts at byte 0x1ec
1531	relocations
11	flags
72	bytes extra data
942532	bytes needed to load this file
235276	bytes Zehn executable file
Entry point:	0x0
Application: N-Launcher
Author: daniel.b
Version: 3
Notice: Console Game Library Launcher for TI-Nspire CX II-T
This executable uses the new lcd_blit API.
This executable supports the HW-W 240x320 LCD.
```

- **Taille exacte de `N-Launcher.tns`** : `235 768` octets (`230,2 Ko`).
- **Taille exacte de `N-Launcher.elf`** : `810 068` octets (`791,1 Ko`).
- **Taille exacte de `nl_boot_stub.tns`** : `53 284` octets (`52,0 Ko`, embarqué dans `startup_stub_data.h`).
- **SHA-256 (`N-Launcher.tns`)** : `50bde3eab6cf89a0c4c671b8eae014d567f063bd611c3adc7b8fe51c772e59d1`.
- **SHA-256 (`nl_boot_stub.tns`)** : `59ca1bdaca88f89fe9830c78d968fdbc0d81dd74118a3dd0f1924877a275ef18`.
- **Signature externe (`0x000`)** : `50 52 47 00` (`"PRG\0"`).
- **Signature interne (`0x1ec`)** : `5a 65 68 6e` (`"Zehn"` version `1`, Version app `3`, Auteur `daniel.b`, `1531` relocations ARM).

---

## 3. Points Techniques Validés (v3.1)

1. **Animations Paramètres (goutte liquide) & Catégories (`TAB`)** :
   - Ouverture/fermeture organique asymétrique centrée autour de l’engrenage `(302, 12)` (`NL_SETTINGS_BLOB_DATA`) avec rotation fluide de l’icône d’engrenage.
   - Sélection continue de liste (`selY - listScrollPx`), glissement fluide de l’indicateur de touche dans `Touches de lancement` (`scKeyX/Y/W`), et capsule `TAB` glissante entre `TOUS`, `FAV` et `REC`.
2. **Recherche directe en bibliothèque & Suppression de `SETTINGS_SEARCH`** :
   - Menu Paramètres consolidé à **10 rubriques** (sans sous-menu Recherche).
   - Saisie directe en bibliothèque filtrant titre/nom de fichier dans la catégorie active, `DEL` conservant la barre ouverte même à 0 caractère, `Espace` autorisé en recherche, `5`/`ENTER` lançant le jeu sélectionné et `ESC` fermant la barre sans changer de catégorie.
3. **Pavé tactile : glissement optionnel & clic central de lancement restauré** :
   - Clic central physique actif en permanence en bibliothèque (même lorsque `touchpadNav == OFF`), protégé contre les rebonds (`waitRelease` + cooldown) et annulé si le doigt glisse de plus de `NL_TP_CLICK_SLIDE_CANCEL` (`140`).
4. **Correctif pagination montante & Mode Gris (`DOCK`)** :
   - Élimination du chevauchement de la ligne 0 sur la ligne 1 lors de la remontée de page (`vSettled && searchH === 0`).
   - 3e thème **Mode Gris** intégré sur les 4 couleurs d’accent (**12 combinaisons**), cycle `DOCK` **Sombre → Clair → Gris → Sombre** avec diffusion d’encre organique et persistance dans `nlauncher.cfg.tns`.
5. **Indicateur batterie optique TI-OS, Grilles 1:1, `.ico` Windows, Capture VRAM** :
   - ROI optique TI-OS (3 niveaux + Indisponible, rafraîchissement 60 min + retour de jeu), 5 grilles carrées `1:1`, décodeur `.ico` (`1..32 bpp`/PNG/AND), 7 tris, stats RTC `0x90090000`, capture VRAM multi-canal et boucle Timer 2 SP804 sans `WFI`.
6. **Protection Anti-Boucle de Redémarrage, Mode Récupération `ESC`/`ON` & Durcissement Pile ARM** :
   - Garde `nl_boot_guard.cfg.tns` (`pending=1` écrit avant `nl_exec()`, validé uniquement après `30` frames stables ou sortie propre), coupure automatique in-place (`autostart=0` sans altérer favoris/thèmes/stats/raccourcis), touche de secours `ESC`/`ON` au démarrage, suppression des grands tampons sur pile ARM (`< 1,9 Ko` max vérifié via `-fstack-usage`) et validation des pointeurs VRAM/MMU.

---

## 4. Tests Automatisés Exécutés (`native/test_runner.c` & `nlauncherEngine.test.ts`)

```text
============================================================
 N-LAUNCHER v3 — MODULAR C VERIFICATION & BINARY TEST SUITE
============================================================
[PASS] Compiled ARM Binary './N-Launcher.tns' verified (235768 bytes, Zehn v1, ver 3, relocs 1531).
[PASS] Strict 3 categories verified: TOUS, FAV, REC.
[PASS] Cover aspect crop, black/blank rejection, VRAM capture & no-overwrite rule verified.
[PASS] Multi-format & multi-game capture suite (white-bg, primary-color, GBC, 555, 4bpp, staging, no-overwrite).
[PASS] Vertical scroll (156px), interruptible card scale, dot glide & launch guard verified.
[PASS] DOCK light/dark, MENU 4-color cycle, persistence, 8 combinations & pagination verified.
[PASS] Framebuffer rendering verified (splash, subtle nav highlight, rounded clip, 17px bottom peek, launch zoom).
[PASS] Startup logo zoom/pulse, 8-dot spinner (8 themes), progressive scan & resilient persistence verified.
[PASS] Organic ink diffusion transition (asymmetry=18 px, soft edge, interruptible) verified.
[PASS] Settings menu, 5 layouts, show_names, autostart pre-flight, shortcuts, 7 sorts, stats, search, safe reset, fluid animations (A-G), touchpad navigation, optical battery indicator & visual icon hierarchy verified.
[PASS] Settings shortcut (Scratchpad/Save key + Ctrl+S), anti-bounce guard & Settings open/ESC cycle verified.
[PASS] Gray mode, touchpad click guard, upward pagination zero-overlap, liquid-drop Settings, TAB slide & key-picker glide verified.
[PASS] 4-Layer Launch Zoom (shrink + rings behind card + .scr.tns crossfade), Windows .ico (/ndless/icons/) & Gestion des Icones verified.
[PASS] All 5 square icon layouts, 16:9 RGBA multi-entry .ico & adaptive dash pagination verified.
[PASS] Indexed 1/4/8bpp ICOs, AND masks, PNG-compressed ICOs, symmetric alpha trim & .ico fallback verified.
[PASS] Boot-Guard crash-loop protection, deferred stabilization, ESC/ON recovery & data preservation verified.
============================================================
 ALL NATIVE C v3.1 TESTS PASSED SUCCESSFULLY (100%)
============================================================
```

> **Note de couverture** : Les suites C (`test_runner.c`, 16/16 blocs réussis, 0 warning) et TypeScript (`nlauncherEngine.test.ts`, 16/16 tests réussis) valident l'ensemble de la logique, de la protection anti-boucle (crash mid-init via `setjmp`/`longjmp`, retour d'erreur `1`, `0xDEAD`, `ESC`/`ON`, préservation des données), du binaire ARMv5TE `N-Launcher.tns` (`235 768` octets), du bootstrapper `nl_boot_stub.tns` (`53 284` octets) et du rendu sur hôte Linux et simulateur Canvas ; la validation matérielle finale sur **TI-Nspire CX II-T** physique reste à effectuer après transfert du nouveau `N-Launcher.tns`.
