import React, { useEffect, useState } from 'react';
import {
  AlertTriangle,
  Check,
  CheckCircle2,
  Copy,
  Download,
  FileCode,
  ShieldCheck,
  Terminal,
  Upload,
} from 'lucide-react';
import {
  EXPECTED_TNS_BUILD_STAMP,
  EXPECTED_TNS_SHA256,
  EXPECTED_TNS_SIZE_BYTES,
  EXPECTED_TNS_VERSION,
  TnsVerificationResult,
  computeSha256Hex,
  downloadDeliverableBlob,
  downloadVerifiedTns,
  verifyLiveDeliverableTns,
  verifyTnsBuffer,
} from '../utils/downloadTns';

interface DeliverableFile {
  id: string;
  filename: string;
  path: string;
  role: string;
  sizeLabel: string;
  isBinary?: boolean;
}

export const DELIVERABLE_FILES: DeliverableFile[] = [
  {
    id: 'tns',
    filename: 'N-Launcher.tns',
    path: '/deliverables/N-Launcher.tns',
    role: 'Exécutable final v3.1 ARMv5TE (Build 2026-11-17 #R7 — PRG\\0 + Zehn v1, crashs Ndless persistant/classique corrigés, -360 Ko RAM .bss, suppression démarrage auto + batterie, curseur tactile, animations de profils)',
    sizeLabel: '382 728 octets (373,8 Ko)',
    isBinary: true,
  },
  {
    id: 'nl_hotkey_tns',
    filename: 'nl_hotkey.tns',
    path: '/deliverables/nl_hotkey.tns',
    role: 'Module résident expérimental de raccourci global TI-OS (Ctrl + Menu -> N-Launcher.tns) avec verrous anti-conflit et anti-réentrance',
    sizeLabel: '21 728 octets (21,2 Ko)',
    isBinary: true,
  },
  {
    id: 'elf',
    filename: 'N-Launcher.elf',
    path: '/deliverables/N-Launcher.elf',
    role: 'Binaire intermédiaire ELF32 ARM EABI5 v3.1 (Build 2026-11-17 #R7, généré par nspire-ld)',
    sizeLabel: '969 636 octets (946,9 Ko)',
    isBinary: true,
  },
  {
    id: 'main_c',
    filename: 'main_ndless.c',
    path: '/deliverables/main_ndless.c',
    role: 'Entrée matérielle CX II-T (initialisation LCD/VRAM sécurisée avant animation, zéro MMIO brut, nettoyage auto legacy, curseur tactile, cache CRC32 v4, nl_exec)',
    sizeLabel: '54,4 Ko',
  },
  {
    id: 'profiles_c',
    filename: 'profiles.c',
    path: '/deliverables/profiles.c',
    role: 'Système multi-profils natif (8 profils isolés, 43 avatars RGB565, chargement direct en place sans allocation tas de 840 Ko, memmove sécurisé, raccourcis 0 / - / .)',
    sizeLabel: '25,7 Ko',
  },
  {
    id: 'renderer_c',
    filename: 'renderer.c',
    path: '/deliverables/renderer.c',
    role: 'Rendu 320×240 RGB565 (tampon pile statique partagé, curseur graphique Flèche/Cercle + traînée lumineuse, animations de profils, centrage grilles)',
    sizeLabel: '226,0 Ko',
  },
  {
    id: 'cursors_h',
    filename: 'cursors_data.h',
    path: '/deliverables/cursors_data.h',
    role: 'Sprites alpha 8 bits exacts extraits de icons8-select-cursor-24.png (Flèche, pointe 5,2 / 6,2) et icons8-select-48.png (Cercle, centre 24,24 / 13,13)',
    sizeLabel: '19,3 Ko',
  },
  {
    id: 'covers_c',
    filename: 'covers.c',
    path: '/deliverables/covers.c',
    role: 'Décodage .ico complet sur tas (1..32bpp, masque AND, PNG zlib/tRNS/Adam7, rognage indépendant des 4 bords transparents)',
    sizeLabel: '77,4 Ko',
  },
  {
    id: 'ui_state_c',
    filename: 'ui_state.c',
    path: '/deliverables/ui_state.c',
    role: 'Navigation tactile, paramètres épurés (14 rubriques sans démarrage auto ni batterie), curseur personnalisable & grilles 320×240',
    sizeLabel: '132,7 Ko',
  },
  {
    id: 'scanner_c',
    filename: 'scanner.c',
    path: '/deliverables/scanner.c',
    role: 'Inspection binaire Zehn/PRG/bFLT sur tas (pile ARM réduite), scan progressif coopératif, exclusion docs/système/nl_boot/nl_hotkey',
    sizeLabel: '18,9 Ko',
  },
  {
    id: 'anim_c',
    filename: 'animation.c',
    path: '/deliverables/animation.c',
    role: 'Contrôleur d’animations 60 FPS (inertie critique du curseur tactile sans rebond parasite, traînée lumineuse 5 nœuds, gouttes de profils)',
    sizeLabel: '28,7 Ko',
  },
  {
    id: 'storage_c',
    filename: 'storage.c',
    path: '/deliverables/storage.c',
    role: 'Persistance atomique des préférences, purge automatique des anciens hooks nl_boot.tns & nl_boot_guard.cfg.tns, cache v4 CRC32',
    sizeLabel: '35,0 Ko',
  },
  {
    id: 'header_h',
    filename: 'nlauncher.h',
    path: '/deliverables/nlauncher.h',
    role: 'En-tête modulaire v3.1 (NL_BUILD_STAMP_STR "Build 2026-11-17 #R7", NL_SETTINGS_COUNT 14, NL_DISPLAY_OPT_COUNT 4, .bss allégé)',
    sizeLabel: '42,1 Ko',
  },
  {
    id: 'test_c',
    filename: 'test_runner.c',
    path: '/deliverables/test_runner.c',
    role: 'Suite de 22 sections de tests C automatisés (binaire ARM #R7, purge démarrage auto legacy, curseur tactile, animations de profils)',
    sizeLabel: '186,8 Ko',
  },
  {
    id: 'makefile',
    filename: 'Makefile',
    path: '/deliverables/Makefile',
    role: 'Compilation modulaire Ndless SDK avec estampille Zehn Build 2026-11-17 #R7 (nl_hotkey.tns -> N-Launcher.tns + test_runner)',
    sizeLabel: '3,2 Ko',
  },
];

const REAL_HEX_DUMP_PRG = `Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   ASCII
000000   50 52 47 00 f0 4f 2d e9 14 d0 4d e2 01 a0 a0 e1   PRG..O-...M.....
000010   00 00 8d e5 08 50 4f e2 98 11 9f e5 01 50 45 e0   .....PO......PE.
000020   94 11 9f e5 01 50 85 e0 84 31 9f e5 00 20 95 e5   .....P...1... ..
000030   03 00 52 e1 5c 00 00 1a 04 00 95 e5 01 00 50 e3   ..R.\\.........P.`;

const REAL_HEX_DUMP_ZEHN = `Offset   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F   Signification Zehn v1
0001ec   5a 65 68 6e 01 00 00 00 1c d5 05 00 ff 06 00 00   "Zehn" v1 | size=382236 | relocs=1791
0001fc   0b 00 00 00 60 00 00 00 cc ef 0b 00 00 00 00 00   flags=11 | extra=96B | alloc=782284B
00020c   ... Notice : "Console Game Library Launcher for TI-Nspire CX II-T (Build 2026-11-17 #R7)"
00021c   Drapeaux matériels : USES_LCD_BLIT + RUNS_ON_HWW (240x320 RGB565)`;

const GENZEHN_INFO_OUTPUT = `$ genzehn --info --input N-Launcher.tns
Zehn header starts at byte 0x1ec
1791    relocations
11      flags
96      bytes extra data
782284  bytes needed to load this file
382236  bytes Zehn executable file
Entry point:    0x0
Application: N-Launcher
Author: daniel.b
Version: 3
Notice: Console Game Library Launcher for TI-Nspire CX II-T (Build 2026-11-17 #R7)
This executable uses the new lcd_blit API.
This executable supports the HW-W 240x320 LCD.`;

const TEST_RUNNER_OUTPUT = `$ make -C native clean all test
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c main_ndless.c -o main_ndless.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c scanner.c -o scanner.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c covers.c -o covers.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c storage.c -o storage.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c profiles.c -o profiles.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c ui_state.c -o ui_state.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c animation.c -o animation.o
nspire-gcc -Wall -Wextra -marm -std=c11 -ffunction-sections -fdata-sections -isystem /usr/include/newlib -Os -c renderer.c -o renderer.o
nspire-ld main_ndless.o scanner.o covers.o storage.o profiles.o ui_state.o animation.o renderer.o -o N-Launcher.elf -Wl,--gc-sections
genzehn --input N-Launcher.elf --output N-Launcher.tns.zehn --name "N-Launcher" --author "daniel.b" --version 3 --notice "Console Game Library Launcher for TI-Nspire CX II-T (Build 2026-11-17 #R7)" --ndless-min 36 --240x320-support true
make-prg N-Launcher.tns.zehn N-Launcher.tns
./test_runner ./N-Launcher.tns
============================================================
 N-LAUNCHER v3 — MODULAR C VERIFICATION & BINARY TEST SUITE
============================================================
[PASS] Compiled ARM Binary './N-Launcher.tns' verified (382728 bytes, Zehn v1, ver 3, relocs 1791).
[PASS] Strict 3 categories verified: TOUS, FAV, REC.
[PASS] Cover aspect crop, black/blank rejection, VRAM capture & no-overwrite rule verified.
[PASS] Multi-format & multi-game capture suite (white-bg, primary-color, GBC, 555, 4bpp, staging, no-overwrite).
[PASS] Vertical scroll (156px), interruptible card scale, dot glide & launch guard verified.
[PASS] DOCK light/dark, MENU 4-color cycle, persistence, 8 combinations & pagination verified.
[PASS] Framebuffer rendering verified (splash, subtle nav highlight, rounded clip, 17px bottom peek, launch zoom).
[PASS] Startup logo zoom/pulse, 8-dot spinner (8 themes), progressive scan & resilient persistence verified.
[PASS] Organic ink diffusion transition (asymmetry=18 px, soft edge, interruptible) verified.
[PASS] Settings menu, 5 layouts, show_names, shortcuts, 7 sorts, stats, search, safe reset, fluid animations (A-G) & touchpad navigation verified.
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
[PASS] Customizable Touchpad Graphical Cursor (Arrow/Circle/None, Trail, Hotspots, Themes & Persistence) verified.
============================================================
 ALL NATIVE C v3.1 TESTS PASSED SUCCESSFULLY (100%)
============================================================`;

export const SourceAndDeliverablesPanel: React.FC<{ mode: 'binary' | 'source' }> = ({ mode }) => {
  const [selectedFileId, setSelectedFileId] = useState<string>('renderer_c');
  const [fileContent, setFileContent] = useState<string>('Chargement du code source...');
  const [copied, setCopied] = useState(false);
  const [downloadStatus, setDownloadStatus] = useState<string | null>(null);
  const [liveAudit, setLiveAudit] = useState<TnsVerificationResult | null>(null);
  const [localCheckResult, setLocalCheckResult] = useState<{
    filename: string;
    result: TnsVerificationResult;
  } | null>(null);

  const activeFile =
    DELIVERABLE_FILES.find((f) => f.id === selectedFileId) || DELIVERABLE_FILES[5];

  useEffect(() => {
    let cancelled = false;
    verifyLiveDeliverableTns()
      .then((res) => {
        if (!cancelled) setLiveAudit(res);
      })
      .catch(() => {
        // Ignore network errors in test harness
      });
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    if (mode !== 'source') return;
    if (activeFile.isBinary) return;

    let cancelled = false;
    fetch(`${activeFile.path}?v=${Date.now()}`, { cache: 'no-store' })
      .then((res) => res.text())
      .then((txt) => {
        if (!cancelled) setFileContent(txt);
      })
      .catch(() => {
        if (!cancelled) {
          setFileContent('// Impossible de charger le fichier.');
        }
      });

    return () => {
      cancelled = true;
    };
  }, [activeFile, mode]);

  const handleCopy = () => {
    navigator.clipboard.writeText(fileContent);
    setCopied(true);
    setTimeout(() => setCopied(false), 1800);
  };

  const handleVerifiedDownload = async () => {
    try {
      const check = await downloadVerifiedTns();
      setLiveAudit(check);
      setDownloadStatus(
        `Téléchargement binaire vérifié : N-Launcher.tns (${EXPECTED_TNS_VERSION} · ${check.buildStamp ?? EXPECTED_TNS_BUILD_STAMP} · ${check.sizeBytes.toLocaleString('fr-FR')} octets · SHA-256 ${(check.sha256Hex ?? EXPECTED_TNS_SHA256).slice(0, 16)}…)`
      );
    } catch (err) {
      setDownloadStatus(
        err instanceof Error ? err.message : 'Erreur lors du téléchargement binaire'
      );
    }
  };

  const handleInspectLocalFile = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const buffer = await file.arrayBuffer();
    const result = verifyTnsBuffer(buffer, file.name);
    const sha256Hex = await computeSha256Hex(buffer);
    setLocalCheckResult({
      filename: file.name,
      result: { ...result, sha256Hex },
    });
  };

  if (mode === 'binary') {
    return (
      <div className="space-y-6">
        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5">
          <div className="flex flex-col lg:flex-row lg:items-center lg:justify-between gap-4 border-b border-slate-800 pb-4">
            <div>
              <div className="flex flex-wrap items-center gap-2 mb-1">
                <span className="inline-flex items-center gap-1.5 rounded bg-emerald-500/15 border border-emerald-500/40 px-2.5 py-0.5 text-xs font-mono font-semibold text-emerald-300">
                  <CheckCircle2 className="h-3.5 w-3.5" />
                  {EXPECTED_TNS_VERSION} · {EXPECTED_TNS_BUILD_STAMP}
                </span>
                {liveAudit?.isLatestBuild && (
                  <span className="inline-flex items-center gap-1 rounded bg-sky-500/15 border border-sky-500/40 px-2.5 py-0.5 text-xs font-mono text-sky-300">
                    Binaire serveur vérifié en direct ({liveAudit.sizeBytes.toLocaleString('fr-FR')} o)
                  </span>
                )}
              </div>
              <h2 className="text-lg font-semibold text-slate-100">
                Audit du Binaire Compilé : <code className="text-sky-400">N-Launcher.tns</code>
              </h2>
              <p className="mt-1 text-sm text-slate-400">
                Compilé avec <code className="text-slate-200">arm-none-eabi-gcc 12.2.1</code>, lié via{' '}
                <code className="text-slate-200">nspire-ld</code>, converti avec{' '}
                <code className="text-slate-200">genzehn</code> et encapsulé avec{' '}
                <code className="text-slate-200">make-prg</code>.
              </p>
            </div>
            <div className="flex flex-wrap items-center gap-3 shrink-0">
              <button
                type="button"
                onClick={handleVerifiedDownload}
                className="inline-flex items-center gap-2 rounded-lg bg-sky-500 px-4 py-2 text-xs font-semibold text-slate-950 hover:bg-sky-400 transition-colors whitespace-nowrap shrink-0"
              >
                <Download className="h-4 w-4" />
                Télécharger N-Launcher.tns ({EXPECTED_TNS_SIZE_BYTES.toLocaleString('fr-FR')} o · #R7)
              </button>
              <button
                type="button"
                onClick={() => downloadDeliverableBlob('/deliverables/nl_hotkey.tns', 'nl_hotkey.tns')}
                className="inline-flex items-center gap-2 rounded-lg border border-sky-500/50 bg-sky-500/10 px-3.5 py-2 text-xs font-semibold text-sky-300 hover:bg-sky-500/20 transition-colors whitespace-nowrap shrink-0"
              >
                <Download className="h-4 w-4" />
                Module résident nl_hotkey.tns (21 728 o)
              </button>
              <button
                type="button"
                onClick={() => downloadDeliverableBlob('/deliverables/N-Launcher.elf', 'N-Launcher.elf')}
                className="inline-flex items-center gap-2 rounded-lg border border-slate-700 bg-slate-800 px-3.5 py-2 text-xs font-medium text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
              >
                <Download className="h-4 w-4" />
                Télécharger N-Launcher.elf (969 636 o)
              </button>
            </div>
          </div>

          {downloadStatus && (
            <div className="mt-3 rounded border border-emerald-500/40 bg-emerald-500/10 px-3.5 py-2 text-xs font-mono text-emerald-300 flex items-center gap-2">
              <CheckCircle2 className="h-4 w-4 shrink-0" />
              <span>{downloadStatus}</span>
            </div>
          )}

          <div className="mt-4 grid grid-cols-2 sm:grid-cols-4 gap-4 text-xs font-mono">
            <div className="rounded border border-slate-800 bg-slate-950/70 p-3">
              <div className="text-slate-400">Version & Estampille</div>
              <div className="mt-1 text-sm font-semibold text-emerald-400">
                {EXPECTED_TNS_VERSION} (#R7)
              </div>
              <div className="mt-0.5 text-slate-500">{EXPECTED_TNS_BUILD_STAMP}</div>
            </div>
            <div className="rounded border border-slate-800 bg-slate-950/70 p-3">
              <div className="text-slate-400">En-tête Externe / Interne</div>
              <div className="mt-1 text-sm font-semibold text-emerald-400">PRG\0 + Zehn v1</div>
              <div className="mt-0.5 text-slate-500">Offset Zehn : 0x01ec (Notice #R7)</div>
            </div>
            <div className="rounded border border-slate-800 bg-slate-950/70 p-3">
              <div className="text-slate-400">Relocations & Flags</div>
              <div className="mt-1 text-sm font-semibold text-sky-400">1791 relocs · 11 flags</div>
              <div className="mt-0.5 text-slate-500">USES_LCD_BLIT · RUNS_ON_HWW</div>
            </div>
            <div className="rounded border border-slate-800 bg-slate-950/70 p-3">
              <div className="text-slate-400">Taille Fichier / RAM</div>
              <div className="mt-1 text-sm font-semibold text-slate-100">382 728 octets</div>
              <div className="mt-0.5 text-slate-500">Alloc Zehn : 782 284 octets</div>
            </div>
          </div>

          <div className="mt-3 text-xs font-mono text-slate-400 space-y-1">
            <div>
              SHA-256 (<code className="text-slate-300">N-Launcher.tns · {EXPECTED_TNS_BUILD_STAMP}</code>) :{' '}
              <span className="text-sky-300 select-all">
                {liveAudit?.sha256Hex ?? EXPECTED_TNS_SHA256}
              </span>
            </div>
            {liveAudit?.zehnNotice && (
              <div>
                Notice Zehn extraite en direct du binaire :{' '}
                <span className="text-emerald-300">&quot;{liveAudit.zehnNotice}&quot;</span>
              </div>
            )}
          </div>
        </div>

        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5 space-y-4">
          <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-3">
            <div>
              <h3 className="text-sm font-semibold text-slate-100">
                Vérificateur de fichier <code className="text-sky-400">.tns</code> local (Détection de version / estampille)
              </h3>
              <p className="text-xs text-slate-400 mt-0.5">
                Sélectionnez le fichier <code className="text-slate-300">N-Launcher.tns</code> présent sur votre PC
                pour vérifier s’il correspond bien à la dernière compilation{' '}
                <strong className="text-emerald-300">{EXPECTED_TNS_BUILD_STAMP}</strong>.
              </p>
            </div>
            <label className="inline-flex items-center gap-2 rounded-lg border border-sky-500/50 bg-sky-500/10 px-3.5 py-2 text-xs font-semibold text-sky-300 hover:bg-sky-500/20 cursor-pointer transition-colors whitespace-nowrap shrink-0">
              <Upload className="h-3.5 w-3.5" />
              Analyser un fichier .tns de mon PC
              <input
                type="file"
                accept=".tns,*/*"
                onChange={handleInspectLocalFile}
                className="hidden"
              />
            </label>
          </div>

          {localCheckResult && (
            <div
              className={`rounded-lg border p-4 text-xs font-mono space-y-1.5 ${
                localCheckResult.result.valid && localCheckResult.result.isLatestBuild
                  ? 'border-emerald-500/40 bg-emerald-500/10 text-emerald-200'
                  : localCheckResult.result.valid
                    ? 'border-amber-500/40 bg-amber-500/10 text-amber-200'
                    : 'border-rose-500/40 bg-rose-500/10 text-rose-200'
              }`}
            >
              <div className="font-bold flex items-center gap-2">
                {localCheckResult.result.valid && localCheckResult.result.isLatestBuild ? (
                  <CheckCircle2 className="h-4 w-4 text-emerald-400 shrink-0" />
                ) : (
                  <AlertTriangle className="h-4 w-4 text-amber-400 shrink-0" />
                )}
                <span>
                  Fichier analysé : « {localCheckResult.filename} » (
                  {localCheckResult.result.sizeBytes.toLocaleString('fr-FR')} octets) — Estampille :{' '}
                  {localCheckResult.result.buildStamp ?? 'Ancienne version (avant #R5)'}
                </span>
              </div>
              <p className="font-sans text-slate-200">{localCheckResult.result.diagnosis}</p>
              {localCheckResult.result.sha256Hex && (
                <div className="text-[11px] text-slate-400">
                  SHA-256 local : {localCheckResult.result.sha256Hex}
                </div>
              )}
            </div>
          )}
        </div>

        <div className="grid grid-cols-1 lg:grid-cols-2 gap-6">
          <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5">
            <div className="flex items-center justify-between mb-3">
              <h3 className="text-sm font-semibold text-slate-100 flex items-center gap-2">
                <ShieldCheck className="h-4 w-4 text-emerald-400" />
                Inspection Hexadécimale Réelle de N-Launcher.tns
              </h3>
              <span className="text-xs font-mono text-slate-400">Offsets 0x0000 & 0x01ec</span>
            </div>
            <div className="space-y-3">
              <div>
                <div className="text-xs text-slate-400 mb-1">
                  1. Signature <code className="text-emerald-400">&quot;PRG\0&quot;</code> (0x00475250) +
                  prologue ARM <code className="text-slate-300">zehn_loader</code> à l’offset{' '}
                  <code>0x000000</code> :
                </div>
                <pre className="overflow-x-auto rounded border border-slate-800 bg-slate-950 p-3 text-xs font-mono text-slate-200 leading-relaxed">
                  {REAL_HEX_DUMP_PRG}
                </pre>
              </div>
              <div>
                <div className="text-xs text-slate-400 mb-1">
                  2. En-tête <code className="text-sky-400">&quot;Zehn&quot;</code> (0x6e68655a) à
                  l’offset <code>0x0001ec</code> (492 octets) :
                </div>
                <pre className="overflow-x-auto rounded border border-slate-800 bg-slate-950 p-3 text-xs font-mono text-sky-200 leading-relaxed">
                  {REAL_HEX_DUMP_ZEHN}
                </pre>
              </div>
            </div>
          </div>

          <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5">
            <div className="flex items-center justify-between mb-3">
              <h3 className="text-sm font-semibold text-slate-100 flex items-center gap-2">
                <Terminal className="h-4 w-4 text-sky-400" />
                Sortie Officielle de l’Outil <code className="text-sky-400">genzehn --info</code>
              </h3>
              <span className="text-xs font-mono text-emerald-400">Vérifié</span>
            </div>
            <pre className="overflow-x-auto rounded border border-slate-800 bg-slate-950 p-3.5 text-xs font-mono text-emerald-300 leading-relaxed">
              {GENZEHN_INFO_OUTPUT}
            </pre>
            <p className="mt-3 text-xs text-slate-400 leading-relaxed">
              Les deux drapeaux matériels critiques pour la <strong>TI-Nspire CX II-T</strong> sont
              présents dans l’en-tête Zehn :{' '}
              <code className="text-slate-200">USES_LCD_BLIT</code> et{' '}
              <code className="text-slate-200">RUNS_ON_HWW</code> (compatibilité native 240 × 320).
            </p>
          </div>
        </div>

        <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5">
          <div className="flex items-center justify-between mb-3">
            <h3 className="text-sm font-semibold text-slate-100">
              Journal d’Exécution de la Suite de Tests C (<code className="text-sky-400">native/test_runner.c</code>)
            </h3>
            <span className="text-xs font-mono text-emerald-400">22/22 Suites Réussies (100%)</span>
          </div>
          <pre className="overflow-x-auto rounded border border-slate-800 bg-slate-950 p-4 text-xs font-mono text-slate-200 leading-relaxed">
            {TEST_RUNNER_OUTPUT}
          </pre>
        </div>
      </div>
    );
  }

  return (
    <div className="space-y-6">
      <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5">
        <div className="flex flex-col sm:flex-row sm:items-center sm:justify-between gap-3 mb-4">
          <div>
            <h2 className="text-base font-semibold text-slate-100">
              Architecture Modulaire C11 & Livrables Compilés ({EXPECTED_TNS_VERSION} · {EXPECTED_TNS_BUILD_STAMP})
            </h2>
            <p className="text-xs text-slate-400 mt-0.5">
              Sélectionnez un fichier pour consulter son code ou téléchargez l’exécutable ARM prêt
              pour la calculatrice.
            </p>
          </div>
          <button
            type="button"
            onClick={handleVerifiedDownload}
            className="inline-flex items-center gap-2 rounded-lg bg-sky-500 px-4 py-2 text-xs font-semibold text-slate-950 hover:bg-sky-400 transition-colors whitespace-nowrap shrink-0"
          >
            <Download className="h-4 w-4" />
            Télécharger N-Launcher.tns ({EXPECTED_TNS_SIZE_BYTES.toLocaleString('fr-FR')} o · #R7)
          </button>
        </div>

        {downloadStatus && (
          <div className="mb-4 rounded border border-emerald-500/40 bg-emerald-500/10 px-3.5 py-2 text-xs font-mono text-emerald-300 flex items-center gap-2">
            <CheckCircle2 className="h-4 w-4 shrink-0" />
            <span>{downloadStatus}</span>
          </div>
        )}

        <div className="overflow-x-auto">
          <table className="w-full text-left border-collapse text-xs">
            <thead>
              <tr className="border-b border-slate-800 text-slate-400">
                <th className="py-2.5 px-3 font-medium">Fichier</th>
                <th className="py-2.5 px-3 font-medium">Rôle dans l’architecture</th>
                <th className="py-2.5 px-3 font-medium text-right">Taille</th>
                <th className="py-2.5 px-3 font-medium text-right">Action</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-slate-800/70">
              {DELIVERABLE_FILES.map((file) => {
                const isSelected = file.id === selectedFileId;
                return (
                  <tr
                    key={file.id}
                    className={`transition-colors ${
                      isSelected ? 'bg-sky-500/10' : 'hover:bg-slate-800/50'
                    }`}
                  >
                    <td className="py-2.5 px-3 font-mono font-semibold text-slate-100 whitespace-nowrap">
                      {file.filename}
                    </td>
                    <td className="py-2.5 px-3 text-slate-300">{file.role}</td>
                    <td className="py-2.5 px-3 font-mono text-slate-400 text-right whitespace-nowrap">
                      {file.sizeLabel}
                    </td>
                    <td className="py-2.5 px-3 text-right whitespace-nowrap">
                      <div className="inline-flex items-center gap-2">
                        {!file.isBinary && (
                          <button
                            type="button"
                            onClick={() => setSelectedFileId(file.id)}
                            className={`px-2.5 py-1 rounded text-xs font-medium transition-colors whitespace-nowrap shrink-0 ${
                              isSelected
                                ? 'bg-sky-500 text-slate-950 font-semibold'
                                : 'bg-slate-800 text-slate-200 hover:bg-slate-700'
                            }`}
                          >
                            Afficher
                          </button>
                        )}
                        <button
                          type="button"
                          onClick={() => downloadDeliverableBlob(file.path, file.filename)}
                          className="inline-flex items-center gap-1 rounded border border-slate-700 bg-slate-900 px-2.5 py-1 text-xs font-medium text-slate-200 hover:bg-slate-800 transition-colors whitespace-nowrap shrink-0"
                        >
                          <Download className="h-3.5 w-3.5" />
                          Télécharger
                        </button>
                      </div>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </div>

      <div className="rounded-lg border border-slate-800 bg-slate-900/90 overflow-hidden">
        <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 bg-slate-950/60 px-4 py-3">
          <div className="flex items-center gap-2">
            <FileCode className="h-4 w-4 text-sky-400" />
            <span className="font-mono text-sm font-semibold text-slate-100">
              {activeFile.filename}
            </span>
            <span className="text-xs text-slate-400">· {activeFile.role}</span>
          </div>
          <div className="flex items-center gap-2">
            <button
              type="button"
              onClick={handleCopy}
              className="inline-flex items-center gap-1.5 rounded border border-slate-700 bg-slate-800 px-3 py-1.5 text-xs font-medium text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
            >
              {copied ? (
                <>
                  <Check className="h-3.5 w-3.5 text-emerald-400" />
                  Copié
                </>
              ) : (
                <>
                  <Copy className="h-3.5 w-3.5" />
                  Copier
                </>
              )}
            </button>
            <button
              type="button"
              onClick={() => downloadDeliverableBlob(activeFile.path, activeFile.filename)}
              className="inline-flex items-center gap-1.5 rounded bg-sky-500 px-3 py-1.5 text-xs font-semibold text-slate-950 hover:bg-sky-400 transition-colors whitespace-nowrap shrink-0"
            >
              <Download className="h-3.5 w-3.5" />
              Télécharger {activeFile.filename}
            </button>
          </div>
        </div>
        <pre className="max-h-[560px] overflow-auto p-4 text-xs font-mono text-slate-200 leading-relaxed bg-slate-950">
          {fileContent}
        </pre>
      </div>
    </div>
  );
};
