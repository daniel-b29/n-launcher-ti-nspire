# Rapport de Compilation et de Validation Technique — N-Launcher v3.1 (`Build 2026-11-18 #R10`)

## 1. Caractéristiques des Binaires Vérifiés

### `N-Launcher.tns`
| Attribut | Valeur Observée |
| :--- | :--- |
| **Version & Estampille** | `v3.1` · `Build 2026-11-18 #R10` |
| **Taille du fichier `.tns`** | `425 880` octets (`415,9 Ko`) |
| **Taille ELF32 ARM (`N-Launcher.elf`)** | `1 037 312` octets (`1 013,0 Ko`) |
| **Sections ELF32 (`arm-none-eabi-size`)** | `text: 230 800` · `data: 185 500` · **`bss: 700`** (`dec: 417 000`) |
| **En-tête externe (`0x0000`)** | `50 52 47 00` (`"PRG\0"`) |
| **En-tête interne Zehn (`0x01ec`)** | `5a 65 68 6e` (`"Zehn"` v1, `425 388` octets Zehn, `2229` relocations, `11` flags, `96` octets extra, **`426 088` octets alloc**) |
| **Drapeaux matériels Zehn** | `USES_LCD_BLIT` + `RUNS_ON_HWW` (`240×320` RGB565) |
| **SHA-256 vérifié (4 emplacements)** | `e4a7fa5ee614d11ffcbf3f2b22affde909a7ca966923479b51d6e68da5a36dd5` |

### `nl_hotkey.tns` (Module résident raccourci global TI-OS)
| Attribut | Valeur Observée |
| :--- | :--- |
| **Version & Notice Zehn** | `2` · `N-Launcher TI-OS Global Hotkey (get_event, Classique & Persistant, #R10)` |
| **Taille du fichier `.tns`** | `24 208` octets (`23,6 Ko`) |
| **Taille ELF32 ARM (`nl_hotkey.elf`)** | `260 316` octets (`text: 21 068`, `data: 1 972`, `bss: 116`) |
| **Métriques Zehn** | `23 716` o Zehn, `127` relocations, `11` flags, `92` o extra, `23 832` o alloc |
| **SHA-256 vérifié (3 emplacements)** | `611cbe593f8b60bdeb7438d6232c08824d67be7a46f838e96d59d181de9879ba` |

---

## 2. Journal d’Inspection Zehn Réel (`N-Launcher.tns`)

```text
Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   Signification Zehn v1
0001ec   5a 65 68 6e 01 00 00 00 ec 7d 06 00 b5 08 00 00   "Zehn" v1 | size=425388 | relocs=2229
0001fc   0b 00 00 00 60 00 00 00 88 80 06 00 00 00 00 00   flags=11 | extra=96B | alloc=426088B
```

Notice Zehn extraite : `Console Game Library Launcher for TI-Nspire CX II-T (Build 2026-11-18 #R10)`.

---

## 3. Diagnostic & Correctif Raccourci Global TI-OS (`#R10`)

1. **Table d’extensions Ndless (`sc_ext_table` / `sc_addrs_ptr`)** : résolution via l’instruction ARM du vecteur SWI `0x28` et la signature `"NEXT"` (`0x4E455854`) à `*(swi_handler - 2)`, en remplacement du pointeur erroné `0x10000020`.
2. **Exécution immédiate sous Ndless Classique** : `nl_hotkey_set()` écrit le `.tns` embarqué dans `/documents/ndless/startup/nl_hotkey.tns`, recherche un bloc RAM `"NLHK"` existant pour le mettre à jour sans fuite mémoire, ou lance `nl_exec()` sur le module pour installer le hook dans la session courante.
3. **Zéro saturation SWI** : appel direct à `sc_ext_table[10]` (`sc_nl_exec`) sans consommer les 2 emplacements SWI de `ints_swi_handler`, et sortie par `_exit(0)`.
4. **Inhibition du menu contextuel TI-OS** : boucle réentrante autour du `get_event` d’origine, garde contre le double lancement (`nl_active.lock.tns`), vidage clavier sans `WFI`, `refresh_osscr()` et mise à zéro de `*ev = 0`.
5. **Contraintes matérielles honnêtes** : sous Ndless Classique (non persistant), un redémarrage efface la RAM jusqu’à réinstallation de Ndless ; dans la session active (et après ré-installation de Ndless via `startup/`), le raccourci fonctionne à l’identique du mode Persistant.

---

## 4. Suite de Tests C Modulaire (`native/test_runner.c`) — 25/25 (100 %)

```text
============================================================
 N-LAUNCHER v3 — MODULAR C VERIFICATION & BINARY TEST SUITE
============================================================
[PASS] Compiled ARM Binary './N-Launcher.tns' verified (425880 bytes, Zehn v1, ver 3, relocs 2229).
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
[PASS] Legacy autostart cleanup, startup stub purge & config migration verified.
[PASS] Persistent library & pre-decoded icon cache (CRC32), Settings refresh & global TI-OS hotkey verified.
[PASS] Deferred last-used sort on game return & pure white launch ripples across all 4 themes verified.
[PASS] Multi-profile system, 43 exclusive avatars, Quick Selector, 0/-/., isolation & persistence verified.
[PASS] Adaptive 320x240 screen occupancy, incomplete row centering & 4-side .ICO margin trimming verified.
[PASS] 4-stage Profile Switch animation, '+' Ghost Creation, Direct Avatar Selection & Birth Confirmation verified.
[PASS] Mode Souris settings, 3 PNG cursor styles (Fleche/Cercle/Pointeur) & config persistence verified.
[PASS] Complete Mode Souris (Mode1/Mode2, CTRL hold, hardware Y-axis, non-launch hover & universal clicks) verified.
[PASS] Centralized diagnostic system, 12-stage boot forensics, fault isolation & CX II-T 6.2.0 crash fix verified.
[PASS] Global TI-OS Hotkey #R10 (SWI 'NEXT' parse, NLHK RAM block, Classique/Persistant, 3 combos, sc_nl_exec, *ev zero) verified.
============================================================
 ALL NATIVE C v3.1 TESTS PASSED SUCCESSFULLY (100%)
============================================================
```

## 5. Suite TypeScript & Intégrité des Livrables (`nlauncherEngine.test.ts`)

- Validation du moteur de simulation (grilles, tris, `.ico`, profils, Mode 1 / Mode 2 Souris, diagnostic `E102..E901`, et raccourci TI-OS `#R10` : 3 combinaisons `ctrl_menu`/`shift_menu`/`ctrl_shift_menu`, `hotkeyRamActive`, clics souris Ligne 0 / Ligne 1 et réinitialisation `resetSettingsOnly`).
- Contrôle SHA-256 et en-tête Zehn `#R10` (`allocSize = 426 088` o, `.bss = 700` o) sur les 4 copies synchronisées : `native/N-Launcher.tns`, `N-Launcher.tns`, `public/N-Launcher.tns` et `public/deliverables/N-Launcher.tns`, ainsi que `nl_hotkey.tns` (`24 208` o) sur ses 3 emplacements.
