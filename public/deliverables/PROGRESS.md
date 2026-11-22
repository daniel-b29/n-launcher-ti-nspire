# SUIVI DU DÉVELOPPEMENT — N-LAUNCHER v3.1 (`Build 2026-11-18 #R10`)

Ce document suit l’état réel de chaque module, les correctifs sur **TI-Nspire CX II-T (TI-OS 6.2.0)** et **CX/CX CAS (TI-OS ~4.5.0)** ainsi que les résultats vérifiés des tests.

## État des fonctionnalités

| Module / Exigence | Statut | Détails & Décisions Techniques |
|---|---|---|
| Compilateur & Binaires ARM (`N-Launcher.tns` + `nl_hotkey.tns`) | **VALIDÉE** | **`N-Launcher.tns`** : `425 880` o (`PRG\0` + `Zehn v1` @ `0x1ec`, `2229` relocs, `11` flags, `.bss = 700` o, `alloc_size = 426 088` o, notice `Build 2026-11-18 #R10`, SHA-256 `e4a7fa5ee614d11ffcbf3f2b22affde909a7ca966923479b51d6e68da5a36dd5`). **`nl_hotkey.tns`** : `24 208` o (`127` relocs, `alloc_size = 23 832` o, SHA-256 `611cbe593f8b60bdeb7438d6232c08824d67be7a46f838e96d59d181de9879ba`). |
| Raccourci global TI-OS Classique & Persistant (`nl_hotkey.c`, `boot_guard_core.h`) | **VALIDÉE** | 5 causes racines corrigées : décodage réel du vecteur SWI `0x28` (`"NEXT"` à `swi_handler - 2`) pour trouver `sc_ext_table` / `sc_addrs_ptr` ; activation live en RAM via `nl_exec()` + bloc partagé `"NLHK"` ; appel direct `sc_ext_table[10]` (`sc_nl_exec`, 0 emplacement SWI consommé) + `_exit(0)` ; wrapper réentrant sur `get_event` mettant `*ev = 0` après déclenchement ; suppression de `nl_no_scr_redraw()`. 3 combinaisons : `Ctrl+Menu`, `Shift+Menu`, `Ctrl+Shift+Menu`. Limitation matérielle Classique documentée (reboot efface RAM jusqu’à réinstallation de Ndless). |
| Correctif crashs TI-Nspire CX II-T (OS 6.2.0) | **VALIDÉE** | **1) Ndless Persistant** : `.bss` réduit à `700` o (`alloc_size = 426 088` o) supprimant le crash avant splash. **2) Ndless Classique** : libération séquentielle du tampon splash avant le chargement du cache/icônes évitant le pic tas et le redémarrage post-animation. |
| Diagnostic centralisé & journal permanent (`diag.c`) | **VALIDÉE** | Codes structurés `E102..E901` (dont `E701` sur échec hook RAM `nl_hotkey`), journal persistant `nlauncher.diag.log.tns`, marqueurs de boot sur 12 étapes (`nlauncher.stage.tns`) et écran *Paramètres → Diagnostics & Journaux*. |
| Isolation des défauts (`scanner.c`, `covers.c`, `storage.c`, `profiles.c`) | **VALIDÉE** | Jeu corrompu isolé (`E201`), icône absente/corrompue remplacée sans bloquer le jeu (`E102`/`E103`), capture absente (`E104`), config/profils sauvegardés en `.bak.tns` (`E301`/`E601`), dossiers recréés (`E401`), libération mémoire sur OOM (`E801`). |
| Mode 1 (Pavé tactile), Mode 2 (Souris), CTRL & 3 curseurs | **VALIDÉE** | Mode 1 historique sans curseur ; Mode 2 Souris (`320 × 240`, axe Y matériel, vitesse 1..5, traînée 5 nœuds, survol non destructif, clics universels incluant *Raccourci TI-OS* et *Diagnostics & Journaux*) ; maintien `CTRL` temporaire ; curseurs Flèche / Cercle / Pointeur animé. |
| Suppression Définitive Autostart / Batterie & Multi-Profils | **VALIDÉE** | `autostart` et batterie supprimés de l’UI et de la logique ; 8 profils isolés, 43 avatars RGB565 et raccourcis `.`, `0`, `-`. |
| Simulateur Web & Vérification téléchargement (`src/`) | **VALIDÉE** | Parité Canvas 320×240 `#R10`, écran *Paramètres → Raccourci TI-OS* 2 lignes + statut RAM/Ndless, audit Zehn en direct (`425 880` o / `.bss 700 o`). |

## Tests Automatisés Vérifiés (`#R10`)

- **Suite C native (`native/test_runner.c`)** : **25/25** suites réussies (`100%`, dont Section 30 `#R10` sur `nl_hotkey.tns`), **0 warning** à la compilation ARM `nspire-gcc`.
- **Suite TypeScript (`src/engine/nlauncherEngine.test.ts`)** : exécutée via `node --import tsx --test` et validée à `100%`.
- **Vérification TypeScript & build Vite** : `compile_applet` et `lint_applet` réussis sans erreur.
