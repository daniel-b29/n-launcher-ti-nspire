# N-Launcher v3.1 (`Build 2026-11-18 #R10`) — Launcher Console pour TI-Nspire CX II-T (Ndless)

**N-Launcher** (`N-Launcher.tns`, version 3.1, estampille `Build 2026-11-18 #R10`, auteur `daniel.b`) est un lanceur de jeux natif écrit en C11 modulaire pour la calculatrice **Texas Instruments TI-Nspire CX II-T (TI-OS 6.2.0)** et compatible **TI-Nspire CX / CX CAS (TI-OS ~4.5.0)** équipées de **Ndless** (**Classique non persistant** et **Persistant**).

---

## 1. Nouveautés `#R10` — Raccourci Global TI-OS (`nl_hotkey.tns`) Classique & Persistant

1. **5 causes racines résolues sur le raccourci global TI-OS (`nl_hotkey.c` + `boot_guard_core.h`)** :
   - **Recherche de table d’extensions Ndless réparée** : `0x10000020` contient la signature TI-OS et non `'NEXT'`. Le module décode désormais le vecteur SWI `0x28` (`ints_swi_handler`), lit `0x4E455854` (`"NEXT"`) à `*(swi_handler - 2)` et retrouve de manière fiable `sc_ext_table` et `sc_addrs_ptr`.
   - **Activation live en RAM dès la session courante (Ndless Classique)** : activer le raccourci dans *Paramètres → Raccourci TI-OS* exécute immédiatement `nl_hotkey.tns` en RAM via `nl_exec()` (en plus de créer `/documents/ndless/startup/nl_hotkey.tns`), et réutilise le bloc partagé `"NLHK"` déjà résident pour basculer ON/OFF ou changer de combinaison sans fuite `execmem`.
   - **Élimination des crashs de récursion SWI (pile Ndless à 2 emplacements)** : `nl_get_event_wrapper()` n’invoque plus d’appel SWI `nl_exec()` depuis l’intérieur d’un hook ; il appelle directement `sc_ext_table[10]` (`sc_nl_exec` -> `ld_exec_with_args`) avec `0` emplacement SWI consommé et utilise des variables 100 % sur pile ainsi que `_exit(0)` pour préserver le module résident en mémoire.
   - **Interception réelle des événements de réveil et suppression du menu contextuel TI-OS** : le wrapper sur `get_event` détecte le raccourci choisi au réveil clavier, vérifie les verrous anti-réentrance et `/documents/ndless/nl_active.lock.tns`, lance `N-Launcher.tns` une seule fois, vide les touches sans `WFI` (`idle()`), appelle `refresh_osscr()` puis **met `*ev = 0` avant de rendre la main** pour empêcher TI-OS d’ouvrir son menu contextuel sur `Ctrl + Menu`.
   - **Suppression de `nl_no_scr_redraw()`** pour préserver le rafraîchissement normal de l’écran TI-OS au retour.
2. **3 combinaisons configurables sans lancement automatique au démarrage** :
   - Choix entre **`Ctrl + Menu`**, **`Shift + Menu`** et **`Ctrl + Shift + Menu`** dans *Paramètres → Raccourci TI-OS* (clavier ou Souris).
   - **Aucun lancement automatique au boot** : `nl_boot.tns` reste définitivement supprimé ; le raccourci ne déclenche N-Launcher que sur appui volontaire dans TI-OS.
   - **Limitation inhérente à Ndless Classique (non persistant)** : sous Ndless Classique, un redémarrage complet ou Reset efface la RAM (donc Ndless et le hook `"NLHK"`). Dès la réinstallation de Ndless, `/documents/ndless/startup/nl_hotkey.tns` ré-attache automatiquement le hook ; sous Ndless Persistant, il reste actif immédiatement après redémarrage.
3. **Socle `#R9` conservé** :
   - Correctif CX II-T 6.2.0 (`.bss = 700` o, `alloc_size = 426 088` o, libération séquentielle du splash avant chargement du cache CRC32), diagnostic centralisé 12 étapes & codes `E102..E901` (`E701` en cas d’échec hook RAM), Mode 1 (Pavé tactile) / Mode 2 (Souris) + 3 curseurs, 8 profils & 43 avatars.

---

## 2. Installation sur la TI-Nspire CX II-T (TI-OS 6.2.0)

1. Téléchargez **`N-Launcher.tns`** (`425 880` octets · SHA-256 `e4a7fa5ee614d11ffcbf3f2b22affde909a7ca966923479b51d6e68da5a36dd5` — conservez exactement le nom `N-Launcher.tns`).
2. Copiez **`N-Launcher.tns`** dans `/documents/ndless/` ou `/documents/`. *(Optionnel : placez également `nl_hotkey.tns` dans `/documents/ndless/startup/`, sinon N-Launcher l’y extrait automatiquement dès l’activation du réglage).*
3. Placez vos jeux `.tns` dans `/documents/games/`, `/documents/ndless/`, `/documents/roms/` ou tout sous-dossier de `/documents/`.
4. Lancez **`N-Launcher.tns`** avec **`EXE`**, ouvrez **Paramètres → Raccourci TI-OS**, activez le raccourci et choisissez votre combinaison (`Ctrl+Menu`, `Shift+Menu` ou `Ctrl+Shift+Menu`).
5. Quittez vers TI-OS (`ESC`/`ON`), puis testez le raccourci choisi depuis l’écran d’accueil TI-OS.

---

## 3. Commandes sur la Calculatrice

| Touche Physique TI-Nspire | Action |
| :--- | :--- |
| **`Ctrl+Menu` / `Shift+Menu` / `Ctrl+Shift+Menu`** *(dans TI-OS)* | Lancer directement **N-Launcher** depuis les menus TI-OS (si activé dans Paramètres) |
| **Flèches directionnelles** (ou `4`/`6`/`8`/`2`) | Naviguer dans la grille ou les paramètres |
| **Pavé tactile (Mode 1)** | Clic central = lancer le jeu ; glissement (si activé) = changer d’élément |
| **Pavé tactile (Mode 2 — Souris)** | Glisser = déplacer le curseur 2D ; clic = activer l’élément survolé |
| **`CTRL` maintenu** *(en Mode 1)* | Active temporairement le mode Souris jusqu’au relâchement |
| **`Save` (`Ctrl+S`)** | Ouvrir / fermer le **Menu Paramètres** |
| **`.` / `0` / `-`** | Sélecteur rapide de profils / profil précédent / profil suivant |
| **Lettres / chiffres (`A..Z`, `0..9`, `_`, `-`)** | Rechercher dans la bibliothèque (ou raccourci de jeu attribué) |
| **`ESC` / `ON`** | Fermer la recherche / revenir en arrière / quitter vers TI-OS |
| **`TAB`** | Basculer entre `TOUS`, `FAV` et `REC` (ou entre onglets de paramètres/diagnostics) |
| **`DOC` (`DOCK`) / `MENU`** | Changer de mode (Sombre / Clair / Gris) et de couleur d’accent |
| **`VAR` / `*` / `+`** | Ajouter ou retirer le jeu sélectionné des **Favoris (`★`)** |
| **`TRIG` / `=`** | Page précédente / Page suivante |

---

## 4. Architecture Modulaire & Compilation (`native/`)

Modules C11 : `main_ndless.c`, `diag.c`, `scanner.c`, `covers.c`, `storage.c`, `profiles.c`, `ui_state.c`, `animation.c`, `renderer.c`, `nl_hotkey.c`, `nlauncher.h`, `cursors_data.h`, `avatars_data.h`, `boot_guard_core.h`, `hotkey_stub_data.h`, `test_runner.c`.

```bash
cd native
make clean all test
```
