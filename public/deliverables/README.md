# N-Launcher v3.1 — Launcher Console pour TI-Nspire CX II-T (Ndless)

**N-Launcher** (`N-Launcher.tns`, version 3.1, auteur `daniel.b`) est un lanceur de jeux natif écrit en C11 modulaire pour la calculatrice **Texas Instruments TI-Nspire CX II-T** (ainsi que TI-Nspire CX / CX CAS) équipée de **Ndless**.

---

## 1. Nouveautés de la Version 3.1 (`N-Launcher.tns` — `235 768` octets)

1. **Animations fluides du Menu Paramètres & des Catégories (`TAB`)** :
   - Ouverture et fermeture en **goutte de liquide organique asymétrique** centrée autour de l’icône d’engrenage `(302, 12)` combinée à une **rotation fluide de l’engrenage**.
   - **Défilement fluide de la sélection** (`set_sel_y_fp` et `set_list_scroll_fp`) et **glissement continu de la surbrillance** dans le sélecteur de touches de lancement (`SETTINGS_SHORTCUTS`).
   - **Transition fluide des catégories (`TAB`)** : capsule active glissante entre **`TOUS`**, **`FAV`** et **`REC`** avec fondu progressif du texte et glissement horizontal de la grille.
   - Ouverture rapide via la touche physique **`Save`** (`Scratchpad` / `Ctrl+S`) ou le bouton engrenage en haut à droite.
2. **Recherche directe dans la bibliothèque & Menu Paramètres à 10 rubriques** :
   - La rubrique « Rechercher un jeu » a été supprimée du Menu Paramètres (désormais composé de **10 rubriques** dotées chacune d’une icône vectorielle dédiée).
   - **Recherche instantanée en bibliothèque** : saisir directement une lettre/chiffre (hors raccourci attribué et hors `5`) ouvre la barre de recherche intégrée tout en conservant la catégorie active (`TOUS`, `FAV` ou `REC`) et le tri choisi ; `DEL` efface sans fermer la barre, `Espace` ajoute un espace pendant la saisie, `5` ou `ENTER` lance le jeu sélectionné, `ESC` ferme la recherche.
3. **Pavé tactile : glissement optionnel + clic central de lancement restauré** :
   - **L’appui physique (clic central) sur le pavé tactile lance le jeu sélectionné** dans la bibliothèque, que l’option « Navigation tactile » soit activée ou non (avec garde anti-rebond et annulation si le doigt glisse avant le clic).
   - **Navigation tactile (`ON/OFF`, `OFF` par défaut)** : active le déplacement du curseur par glissement du doigt sur le pavé tactile.
4. **3e Thème « Mode Gris » (`DOCK`) & 4 Couleurs d’accent (`MENU`)** :
   - **`DOC` (`DOCK`)** bascule désormais en cycle **Sombre → Clair → Gris → Sombre** avec diffusion d’encre organique asymétrique et persistance immédiate dans `nlauncher.cfg.tns`.
   - **`MENU`** change la couleur d’accentuation (**Bleu → Rouge → Vert → Jaune → Bleu**) sur les **12 combinaisons** (`3 modes × 4 accents`).
5. **Correctif d’affichage de pagination montante & Indicateur batterie optique TI-OS** :
   - Suppression du chevauchement vertical de la première ligne lors de la remontée entre pages (clamping `vSettled` préservant l’animation descendante).
   - Lecture optique discrète de l’icône batterie TI-OS au démarrage, à la sortie d’un jeu et toutes les 60 min (option `Afficher la batterie` + rafraîchissement manuel dans **Affichage**).
6. **Démarrage automatique sécurisé anti-boucle & Mode Récupération (`nl_boot.tns`)** :
   - **Protection anti-boucle de redémarrage** : avant de lancer `N-Launcher.tns`, `nl_boot.tns` enregistre `pending=1` dans `/documents/ndless/nl_boot_guard.cfg.tns` ; cet indicateur n’est remis à `0` qu’après stabilisation complète (`30` images fluides dans la bibliothèque) ou sortie propre. Si un crash ou redémarrage survient avant stabilisation, la tentative suivante détecte l’échec, désactive automatiquement `autostart=0` (sans toucher aux jeux, favoris, thèmes, raccourcis ni statistiques) et laisse **TI-OS démarrer normalement**.
   - **Mode de récupération manuelle** : maintenez **`ESC`** ou **`ON`** pendant l’allumage pour annuler immédiatement le lancement automatique, désactiver `autostart=0` et démarrer sur **TI-OS**.
7. **5 grilles repensées (`1×1` à `4×4`), 7 tris, icônes `.ico` & Statistiques RTC** :
   - Décodeur `.ico` Windows complet (`1..32 bpp`, PNG/AND, rognage alpha), grilles carrées `1:1` (`3×2` `84×84` px par défaut), pagination adaptative (`> 19` pages), 7 tris, statistiques RTC `0x90090000`, capture VRAM sans écrasement et réinitialisation sûre.

---

## 2. Installation sur la TI-Nspire CX II-T

1. Téléchargez **`N-Launcher.tns`** (`235 768` octets — conservez exactement ce nom sans espace avant `.tns`).
2. Copiez **`N-Launcher.tns`** dans `/documents/ndless/` ou `/documents/`.
3. Placez vos jeux exécutables `.tns` dans `/documents/games/`, `/documents/ndless/`, `/documents/roms/` ou tout sous-dossier de `/documents/` (les cours TI-Nspire `.tns`, le bootstrapper `nl_boot.tns` et les jaquettes `.ico.tns`/`.bmp.tns` sont filtrés automatiquement).
4. Lancez **`N-Launcher.tns`** depuis la calculatrice avec **`EXE`** (`Enter`).
   > Si vous aviez déjà activé le démarrage automatique avec une ancienne version, réactivez-le une fois depuis **Paramètres → Démarrage automatique** pour installer le nouveau bootstrapper anti-boucle `nl_boot.tns`.

---

## 3. Commandes sur la Calculatrice

| Touche Physique TI-Nspire | Action dans N-Launcher |
| :--- | :--- |
| **Flèches directionnelles** (ou `4`/`6`/`8`/`2`) | Naviguer dans la grille ou les paramètres |
| **Glissement sur le pavé tactile** *(si activé)* | Déplacer la sélection horizontalement et verticalement |
| **Clic central sur le pavé tactile / `ENTER` / `5`** | Lancer le jeu sélectionné dans la bibliothèque |
| **`Save` (`Ctrl+S`)** | Ouvrir / fermer le **Menu Paramètres** |
| **Lettres / chiffres (`A..Z`, `0..9`, `_`, `-`)** | Rechercher directement dans la bibliothèque (ou lancer un jeu si raccourci attribué hors recherche) |
| **`ESC` / `ON`** | Fermer la recherche / revenir à l'écran précédent / à `TOUS` / quitter vers l'OS |
| **`TAB`** | Basculer avec capsule glissante entre **`TOUS`**, **`FAV`** et **`REC`** |
| **`DOC` (`DOCK`)** | Basculer **Mode Sombre → Mode Clair → Mode Gris → Mode Sombre** |
| **`MENU`** | Changer directement la couleur (**Bleu → Rouge → Vert → Jaune → Bleu**) |
| **`VAR` / `*` / `+`** | Ajouter ou retirer le jeu sélectionné des **Favoris (`★`)** |
| **`DEL`** | Effacer un caractère en recherche, supprimer un raccourci ou actualiser le scan |
| **`TRIG` (`7`) / `=` (`9`)** | Page précédente / Page suivante |

---

## 4. Architecture Modulaire & Compilation (`native/`)

Modules C11 : `main_ndless.c`, `scanner.c`, `covers.c`, `storage.c`, `ui_state.c`, `animation.c`, `renderer.c`, `nl_boot_stub.c`, `boot_guard_core.h`, `nlauncher.h`, `startup_stub_data.h`, `test_runner.c`.

```bash
cd native
make clean all test
```
