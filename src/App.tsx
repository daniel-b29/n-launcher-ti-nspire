import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  ArrowUp,
  Camera,
  CheckCircle2,
  Cpu,
  Download,
  FolderSearch,
  ImagePlus,
  Play,
  RotateCcw,
  Settings,
  Star,
} from 'lucide-react';
import {
  ACCENT_COLOR_ORDER,
  AccentColor,
  CATEGORY_LABELS,
  CATEGORY_ORDER,
  CURSOR_STYLE_ORDER,
  CategoryId,
  CursorStyle,
  DISPLAY_MODE_ORDER,
  DisplayMode,
  LAYOUT_LABELS,
  LAYOUT_ORDER,
  LayoutMode,
  LauncherState,
  NL_ICON_MAX_DIM,
  NL_TP_SENS_DEFAULT,
  NavKey,
  SORT_ORDER_LABELS,
  SORT_ORDER_LIST,
  ScanPreferences,
  SortOrder,
  VirtualFileEntry,
  advanceBatchCapture,
  applyCapturedGameIcon,
  applyCapturedScreenshot,
  clampTouchpadSensitivity,
  computeIconFitDims,
  createDefaultSimulatorFilesystem,
  createTouchpadClickTracker,
  createTouchpadTracker,
  decodeWindowsIcoBuffer,
  formatDuration,
  formatTimestamp,
  getActiveProfile,
  getAvatarImageUrl,
  getLayoutMetrics,
  handleCharInput,
  handleLauncherKey,
  processTouchpadClick,
  processTouchpadSample,
  rebuildFilter,
  resetTouchpadClickTracker,
  resetTouchpadTracker,
  scanVirtualFilesystem,
  setTouchpadControl,
  snapshotActiveProfile,
  toggleSimulatedNdlessPersistence,
} from './engine/nlauncherEngine';
import {
  NspireScreenCanvas,
  captureRealGameScreenshotDataUrl,
} from './components/NspireScreenCanvas';
import { FilesystemInspectorPanel } from './components/FilesystemInspectorPanel';
import { SourceAndDeliverablesPanel } from './components/SourceAndDeliverablesPanel';
import {
  EXPECTED_TNS_BUILD_STAMP,
  EXPECTED_TNS_SHA256,
  EXPECTED_TNS_SIZE_BYTES,
  EXPECTED_TNS_VERSION,
  TnsVerificationResult,
  downloadDeliverableBlob,
  downloadVerifiedTns,
  verifyLiveDeliverableTns,
} from './utils/downloadTns';

type ActiveSection = 'simulator' | 'filesystem' | 'binary' | 'source';

const PREFS_STORAGE_KEY = 'nlauncher.prefs.v3';
const LEGACY_PREFS_V2_KEY = 'nlauncher.prefs.v2';
const LEGACY_PREFS_STORAGE_KEY = 'nlauncher.prefs.v1';

function loadSavedPrefs(): ScanPreferences {
  try {
    const raw =
      window.localStorage.getItem(PREFS_STORAGE_KEY) ||
      window.localStorage.getItem(LEGACY_PREFS_V2_KEY) ||
      window.localStorage.getItem(LEGACY_PREFS_STORAGE_KEY);
    if (!raw) return { displayMode: 'dark', accentColor: 'blue' };
    const parsed = JSON.parse(raw) as ScanPreferences;
    const displayMode: DisplayMode =
      parsed.displayMode && DISPLAY_MODE_ORDER.includes(parsed.displayMode)
        ? parsed.displayMode
        : 'dark';
    const accentColor: AccentColor =
      parsed.accentColor && ACCENT_COLOR_ORDER.includes(parsed.accentColor)
        ? parsed.accentColor
        : 'blue';
    const layout: LayoutMode =
      parsed.settings?.layout && LAYOUT_ORDER.includes(parsed.settings.layout)
        ? parsed.settings.layout
        : '3x2';
    const sortOrder: SortOrder =
      parsed.settings?.sortOrder && SORT_ORDER_LIST.includes(parsed.settings.sortOrder)
        ? parsed.settings.sortOrder
        : 'alpha_asc';
    return {
      displayMode,
      accentColor,
      settings: {
        autostart: false,
        globalHotkey: Boolean(parsed.settings?.globalHotkey),
        layout,
        showNames: Boolean(parsed.settings?.showNames),
        showShortcuts: parsed.settings?.showShortcuts ?? true,
        showBattery: false,
        touchpadNav: Boolean(parsed.settings?.touchpadNav),
        touchpadSensitivity:
          typeof parsed.settings?.touchpadSensitivity === 'number'
            ? clampTouchpadSensitivity(parsed.settings.touchpadSensitivity)
            : NL_TP_SENS_DEFAULT,
        touchpadCursorShow: Boolean(parsed.settings?.touchpadCursorShow),
        touchpadCursorStyle:
          parsed.settings?.touchpadCursorStyle &&
          CURSOR_STYLE_ORDER.includes(parsed.settings.touchpadCursorStyle as CursorStyle)
            ? (parsed.settings.touchpadCursorStyle as CursorStyle)
            : 'arrow',
        sortOrder,
      },
      sys: {
        ndlessPersistent: parsed.sys?.ndlessPersistent ?? true,
        bootRecoveryTriggered: false,
        bootRecoveryReason: 'none',
      },
      gamePrefs: parsed.gamePrefs ?? {},
      profiles: parsed.profiles,
    };
  } catch {
    return { displayMode: 'dark', accentColor: 'blue' };
  }
}

function serializeStatePrefs(st: LauncherState): string {
  const snapped = snapshotActiveProfile(st);
  const gamePrefs: NonNullable<ScanPreferences['gamePrefs']> = {};
  for (const g of snapped.games) {
    gamePrefs[g.filename.toLowerCase()] = {
      isFavorite: g.isFavorite,
      lastPlayedRank: g.lastPlayedRank,
      launchCount: g.launchCount,
      playTimeSec: g.playTimeSec,
      lastPlayedTs: g.lastPlayedTs,
      shortcutKey: g.shortcutKey || '',
    };
  }
  const payload: ScanPreferences = {
    displayMode: snapped.displayMode,
    accentColor: snapped.accentColor,
    settings: snapped.settings,
    sys: {
      ndlessPersistent: snapped.sys.ndlessPersistent,
      bootRecoveryTriggered: false,
      bootRecoveryReason: 'none',
    },
    gamePrefs,
    profiles: snapped.profiles,
  };
  return JSON.stringify(payload);
}

export default function App() {
  const [activeSection, setActiveSection] = useState<ActiveSection>('simulator');
  const [virtualFiles, setVirtualFiles] = useState<VirtualFileEntry[]>(() =>
    createDefaultSimulatorFilesystem()
  );
  const [state, setState] = useState<LauncherState>(() =>
    scanVirtualFilesystem(virtualFiles, '/documents/ndless/N-Launcher.tns', [], 2, loadSavedPrefs())
  );
  const stateRef = useRef<LauncherState>(state);
  stateRef.current = state;

  const [canvasScale, setCanvasScale] = useState<1 | 2>(2);
  const [bootSeq, setBootSeq] = useState(0);
  const [downloadBanner, setDownloadBanner] = useState<string | null>(null);
  const [liveBinCheck, setLiveBinCheck] = useState<TnsVerificationResult | null>(null);
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const tpTrackerRef = useRef(createTouchpadTracker());
  const tpClickRef = useRef(createTouchpadClickTracker());
  const tpDraggingRef = useRef(false);

  useEffect(() => {
    let cancelled = false;
    verifyLiveDeliverableTns()
      .then((res) => {
        if (!cancelled) setLiveBinCheck(res);
      })
      .catch(() => {
        // Ignore network errors in test harness
      });
    return () => {
      cancelled = true;
    };
  }, []);

  useEffect(() => {
    try {
      window.localStorage.setItem(PREFS_STORAGE_KEY, serializeStatePrefs(state));
    } catch {
      // Ignore storage quota / restricted iframe storage errors
    }
  }, [
    state.displayMode,
    state.accentColor,
    state.settings,
    state.activeCategory,
    state.selectedPos,
    state.playCounter,
    state.profiles,
    state.sys.ndlessPersistent,
    state.games,
  ]);

  const handleDownloadBinary = async () => {
    try {
      const check = await downloadVerifiedTns();
      setLiveBinCheck(check);
      setDownloadBanner(
        `N-Launcher.tns téléchargé et vérifié (${EXPECTED_TNS_VERSION} · ${check.buildStamp ?? EXPECTED_TNS_BUILD_STAMP} · ${check.sizeBytes.toLocaleString('fr-FR')} octets · signature ${check.outerMagicHex} "PRG\\0" + Zehn v1 @ 0x1ec · SHA-256 ${(check.sha256Hex ?? EXPECTED_TNS_SHA256).slice(0, 16)}…). Transférez ce fichier tel quel sur la calculatrice.`
      );
    } catch (err) {
      setDownloadBanner(
        err instanceof Error ? err.message : 'Erreur lors du téléchargement de N-Launcher.tns'
      );
    }
  };

  const handleCaptureDuringGame = useCallback(
    (gameIndex: number, capturedDataUrl: string) => {
      setState((prev) => applyCapturedScreenshot(prev, gameIndex, capturedDataUrl, true));
    },
    []
  );

  /* Automated batch screenshot capture runner when in SETTINGS_CAPTURE_BATCH */
  useEffect(() => {
    if (
      state.sui.screen !== 'SETTINGS_CAPTURE_BATCH' ||
      !state.sui.capActive ||
      state.sui.capSemiAuto ||
      state.sui.capPos >= state.sui.capTotal
    ) {
      return;
    }

    const timer = window.setTimeout(() => {
      setState((prev) => {
        if (
          prev.sui.screen !== 'SETTINGS_CAPTURE_BATCH' ||
          !prev.sui.capActive ||
          prev.sui.capSemiAuto ||
          prev.sui.capPos >= prev.sui.capTotal
        ) {
          return prev;
        }
        const gameIdx = prev.sui.capQueue[prev.sui.capPos];
        const target = prev.games[gameIdx];
        if (!target) {
          return advanceBatchCapture(prev, false, true, false);
        }
        const dataUrl = captureRealGameScreenshotDataUrl(target, performance.now() + 1250);
        if (dataUrl) {
          const withScr = applyCapturedScreenshot(prev, gameIdx, dataUrl, false);
          return advanceBatchCapture(withScr, true, false, false);
        }
        return advanceBatchCapture(prev, false, false, true);
      });
    }, 520);

    return () => window.clearTimeout(timer);
  }, [
    state.sui.screen,
    state.sui.capActive,
    state.sui.capSemiAuto,
    state.sui.capPos,
    state.sui.capTotal,
  ]);

  const dispatchKey = useCallback(
    (key: NavKey) => {
      setState((prev) => {
        if (key === 'RESCAN' && prev.sui.screen === 'LIBRARY' && !prev.sui.qpOpen) {
          const snapped = snapshotActiveProfile(prev);
          const rescanned = scanVirtualFilesystem(
            virtualFiles,
            '/documents/ndless/N-Launcher.tns',
            prev.games,
            prev.playCounter,
            {
              displayMode: prev.displayMode,
              accentColor: prev.accentColor,
              settings: prev.settings,
              sys: { ndlessPersistent: prev.sys.ndlessPersistent },
              profiles: snapped.profiles,
            }
          );
          return {
            ...rescanned,
            toastMessage: `${rescanned.stats.acceptedGames} JEUX DETECTES`,
            toastSeq: prev.toastSeq + 1,
          };
        }
        return handleLauncherKey(prev, key);
      });
    },
    [virtualFiles]
  );

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      const tag = (e.target as HTMLElement)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
      if (activeSection !== 'simulator') return;

      if ((e.ctrlKey || e.metaKey) && (e.key === 's' || e.key === 'S')) {
        e.preventDefault();
        dispatchKey('SETTINGS');
        return;
      }

      const cur = stateRef.current;
      const scPicking =
        cur.sui.screen === 'SETTINGS_SHORTCUTS' &&
        cur.sui.scPickingKey &&
        !cur.sui.scConflictPrompt;

      if (scPicking && e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
        e.preventDefault();
        setState((prev) => handleCharInput(prev, e.key));
        return;
      }

      const profEditingName =
        cur.sui.screen === 'SETTINGS_PROFILES' &&
        (cur.sui.profMode === 'EDIT_NAME' || cur.sui.profMode === 'CREATE_NAME');

      if (profEditingName && e.key.length === 1 && !e.ctrlKey && !e.metaKey && !e.altKey) {
        if (e.key === ' ') {
          e.preventDefault();
          dispatchKey('SPACE');
          return;
        }
        if (/^[a-zA-Z0-9_-]$/.test(e.key)) {
          e.preventDefault();
          setState((prev) => handleCharInput(prev, e.key));
          return;
        }
      }

      if (
        cur.sui.screen === 'LIBRARY' &&
        cur.runningGameIndex === null &&
        e.key.length === 1 &&
        !e.ctrlKey &&
        !e.metaKey &&
        !e.altKey
      ) {
        if (e.key === '.') {
          e.preventDefault();
          dispatchKey('PROFILE_QUICK');
          return;
        }
        if (!cur.sui.qpOpen && !cur.sui.searchActive && e.key === '0') {
          e.preventDefault();
          dispatchKey('PROFILE_PREV');
          return;
        }
        if (!cur.sui.qpOpen && !cur.sui.searchActive && e.key === '-') {
          e.preventDefault();
          dispatchKey('PROFILE_NEXT');
          return;
        }
        if (!cur.sui.qpOpen) {
          if (e.key === '*') {
            e.preventDefault();
            dispatchKey('FAV');
            return;
          }
          if (e.key === ' ') {
            if (cur.sui.searchActive) {
              e.preventDefault();
              dispatchKey('SPACE');
            }
            return;
          }
          if (/^[a-zA-Z0-9_-]$/.test(e.key)) {
            e.preventDefault();
            setState((prev) => handleCharInput(prev, e.key));
            return;
          }
        }
      }

      switch (e.key) {
        case 'ArrowLeft':
          e.preventDefault();
          dispatchKey('LEFT');
          break;
        case 'ArrowRight':
          e.preventDefault();
          dispatchKey('RIGHT');
          break;
        case 'ArrowUp':
          e.preventDefault();
          dispatchKey('UP');
          break;
        case 'ArrowDown':
          e.preventDefault();
          dispatchKey('DOWN');
          break;
        case 'Enter':
        case '5':
          e.preventDefault();
          if (cur.sui.screen === 'LIBRARY' && cur.runningGameIndex === null) {
            if (!cur.launchHolding) {
              setState((prev) => ({ ...prev, launchHolding: true }));
            }
          } else {
            dispatchKey('EXE');
          }
          break;
        case 'Escape':
          e.preventDefault();
          dispatchKey('ESC');
          break;
        case 'Backspace':
          e.preventDefault();
          dispatchKey('BACKSPACE');
          break;
        case 'Delete':
          e.preventDefault();
          dispatchKey(
            cur.sui.screen === 'LIBRARY' && !cur.sui.searchActive ? 'RESCAN' : 'BACKSPACE'
          );
          break;
        case 'Tab':
          e.preventDefault();
          dispatchKey('TAB');
          break;
        case '*':
          e.preventDefault();
          dispatchKey('FAV');
          break;
        case 'f':
        case 'F':
          e.preventDefault();
          dispatchKey('FAV');
          break;
        case 'd':
        case 'D':
          e.preventDefault();
          dispatchKey('DOCK');
          break;
        case 'm':
        case 'M':
          e.preventDefault();
          dispatchKey('MENU');
          break;
        default:
          break;
      }
    };

    const onKeyUp = (e: KeyboardEvent) => {
      const tag = (e.target as HTMLElement)?.tagName;
      if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;
      if (activeSection !== 'simulator') return;
      if (e.key === 'Enter' || e.key === '5') {
        const cur = stateRef.current;
        if (cur.sui.screen === 'LIBRARY' && cur.runningGameIndex === null && cur.launchHolding) {
          e.preventDefault();
          dispatchKey('EXE');
        }
      }
    };

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
    };
  }, [activeSection, dispatchKey]);

  const handleAddVirtualFile = (entry: VirtualFileEntry) => {
    const nextFiles = [entry, ...virtualFiles];
    setVirtualFiles(nextFiles);
    setState((prev) => {
      const snapped = snapshotActiveProfile(prev);
      return scanVirtualFilesystem(
        nextFiles,
        '/documents/ndless/N-Launcher.tns',
        prev.games,
        prev.playCounter,
        {
          displayMode: prev.displayMode,
          accentColor: prev.accentColor,
          settings: prev.settings,
          sys: { ndlessPersistent: prev.sys.ndlessPersistent },
          profiles: snapped.profiles,
        }
      );
    });
  };

  const handleResetFilesystem = () => {
    const initial = createDefaultSimulatorFilesystem();
    setVirtualFiles(initial);
    setState((prev) => {
      const snapped = snapshotActiveProfile(prev);
      return scanVirtualFilesystem(
        initial,
        '/documents/ndless/N-Launcher.tns',
        [],
        2,
        {
          displayMode: prev.displayMode,
          accentColor: prev.accentColor,
          settings: prev.settings,
          sys: { ndlessPersistent: prev.sys.ndlessPersistent },
          profiles: snapped.profiles,
        }
      );
    });
  };

  const handleSelectSlot = (filteredPos: number) => {
    setState((prev) => {
      const m = getLayoutMetrics(
        prev.settings.layout,
        prev.settings.showNames,
        prev.settings.showShortcuts
      );
      return {
        ...prev,
        selectedPos: filteredPos,
        currentPage: Math.floor(filteredPos / m.perPage),
        navSeq: prev.navSeq + 1,
      };
    });
  };

  const handleSelectCategory = (cat: CategoryId) => {
    setState((prev) =>
      rebuildFilter({
        ...prev,
        activeCategory: cat,
        selectedPos: 0,
        navSeq: prev.navSeq + 1,
      })
    );
  };

  const handleUploadCustomCover = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file || state.filteredIndices.length === 0) return;
    const targetIdx = state.filteredIndices[state.selectedPos];
    const isIcoName =
      file.name.toLowerCase().endsWith('.ico') || file.name.toLowerCase().endsWith('.ico.tns');

    const bufReader = new FileReader();
    bufReader.onload = () => {
      if (bufReader.result instanceof ArrayBuffer) {
        const decoded = decodeWindowsIcoBuffer(new Uint8Array(bufReader.result));
        if (decoded) {
          setState((prev) => applyCapturedGameIcon(prev, targetIdx, decoded.dataUrl, 'ICO'));
          return;
        }
      }

      const reader = new FileReader();
      reader.onload = () => {
        const img = new Image();
        img.onload = () => {
          /* Aspect-preserving resize up to 128x128 (zero cropping, zero distortion) */
          const fit =
            img.width <= NL_ICON_MAX_DIM && img.height <= NL_ICON_MAX_DIM
              ? { width: Math.max(1, img.width), height: Math.max(1, img.height) }
              : computeIconFitDims(img.width, img.height);

          const c = document.createElement('canvas');
          c.width = fit.width;
          c.height = fit.height;
          const ctx = c.getContext('2d');
          if (!ctx) return;
          ctx.imageSmoothingEnabled = true;
          ctx.imageSmoothingQuality = 'high';
          ctx.drawImage(img, 0, 0, img.width, img.height, 0, 0, fit.width, fit.height);
          const dataUrl = c.toDataURL('image/png');
          setState((prev) =>
            applyCapturedGameIcon(prev, targetIdx, dataUrl, isIcoName ? 'ICO' : 'BMP')
          );
        };
        img.src = String(reader.result);
      };
      reader.readAsDataURL(file);
    };
    bufReader.readAsArrayBuffer(file);
    e.target.value = '';
  };

  const selectedGame =
    state.filteredIndices.length > 0
      ? state.games[state.filteredIndices[state.selectedPos]]
      : null;

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col">
      {/* Top Bar Contract: 3 zones separated by gap-8 */}
      <header className="flex items-center justify-between gap-8 px-6 py-4 border-b border-slate-800 bg-slate-950/95 sticky top-0 z-30">
        <a
          href="#top"
          onClick={(e) => {
            e.preventDefault();
            setActiveSection('simulator');
          }}
          className="text-lg font-bold tracking-tight text-slate-100 whitespace-nowrap shrink-0"
        >
          N-Launcher
        </a>

        <nav
          aria-label="Navigation principale"
          className="hidden md:flex items-center gap-6 text-sm font-medium text-slate-400"
        >
          <button
            type="button"
            onClick={() => setActiveSection('simulator')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'simulator'
                ? 'text-sky-400 border-b-2 border-sky-400 font-semibold'
                : ''
            }`}
          >
            Simulateur 320×240
          </button>
          <button
            type="button"
            onClick={() => setActiveSection('filesystem')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'filesystem'
                ? 'text-sky-400 border-b-2 border-sky-400 font-semibold'
                : ''
            }`}
          >
            Détection .tns ({state.stats.acceptedGames}/{state.stats.scannedFiles})
          </button>
          <button
            type="button"
            onClick={() => setActiveSection('binary')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'binary'
                ? 'text-sky-400 border-b-2 border-sky-400 font-semibold'
                : ''
            }`}
          >
            Inspection Binaire
          </button>
          <button
            type="button"
            onClick={() => setActiveSection('source')}
            className={`hover:text-slate-100 transition-colors whitespace-nowrap shrink-0 pb-0.5 ${
              activeSection === 'source'
                ? 'text-sky-400 border-b-2 border-sky-400 font-semibold'
                : ''
            }`}
          >
            Code Source & Livrables
          </button>
        </nav>

        <div className="flex items-center gap-3 shrink-0">
          <span
            className="hidden xl:inline-flex items-center gap-1.5 rounded border border-emerald-500/40 bg-emerald-500/10 px-2.5 py-1 text-[11px] font-mono font-semibold text-emerald-300"
            title={`SHA-256 : ${liveBinCheck?.sha256Hex ?? EXPECTED_TNS_SHA256}`}
          >
            <CheckCircle2 className="h-3.5 w-3.5 text-emerald-400" />
            {EXPECTED_TNS_VERSION} · {liveBinCheck?.buildStamp ?? EXPECTED_TNS_BUILD_STAMP}
          </span>
          <button
            type="button"
            onClick={handleDownloadBinary}
            className="inline-flex items-center gap-2 px-4 py-2 text-xs font-semibold text-slate-950 bg-sky-400 rounded-lg hover:bg-sky-300 transition-colors whitespace-nowrap shrink-0"
          >
            <Download className="h-3.5 w-3.5" />
            Télécharger N-Launcher.tns (#R7)
          </button>
        </div>
      </header>

      {/* Mobile Section Switcher */}
      <div className="flex md:hidden items-center gap-1 px-4 py-2 border-b border-slate-800 bg-slate-900 overflow-x-auto">
        <button
          type="button"
          onClick={() => setActiveSection('simulator')}
          className={`px-3 py-1.5 rounded text-xs font-medium whitespace-nowrap shrink-0 ${
            activeSection === 'simulator'
              ? 'bg-sky-500 text-slate-950 font-semibold'
              : 'text-slate-300'
          }`}
        >
          Simulateur
        </button>
        <button
          type="button"
          onClick={() => setActiveSection('filesystem')}
          className={`px-3 py-1.5 rounded text-xs font-medium whitespace-nowrap shrink-0 ${
            activeSection === 'filesystem'
              ? 'bg-sky-500 text-slate-950 font-semibold'
              : 'text-slate-300'
          }`}
        >
          Détection .tns
        </button>
        <button
          type="button"
          onClick={() => setActiveSection('binary')}
          className={`px-3 py-1.5 rounded text-xs font-medium whitespace-nowrap shrink-0 ${
            activeSection === 'binary'
              ? 'bg-sky-500 text-slate-950 font-semibold'
              : 'text-slate-300'
          }`}
        >
          Inspection Binaire
        </button>
        <button
          type="button"
          onClick={() => setActiveSection('source')}
          className={`px-3 py-1.5 rounded text-xs font-medium whitespace-nowrap shrink-0 ${
            activeSection === 'source'
              ? 'bg-sky-500 text-slate-950 font-semibold'
              : 'text-slate-300'
          }`}
        >
          Sources & Livrables
        </button>
      </div>

      {/* Main Content Container */}
      <main className="flex-1 max-w-7xl w-full mx-auto px-4 sm:px-6 py-6 space-y-6">
        <section className="rounded-lg border border-slate-800 bg-slate-900/80 p-4 space-y-3">
          <div className="flex flex-col lg:flex-row lg:items-center lg:justify-between gap-4">
            <div className="space-y-1">
              <div className="flex flex-wrap items-center gap-2 text-xs text-slate-400 font-mono">
                <span className="text-sky-400 font-semibold">TI-Nspire CX II-T</span>
                <span aria-hidden="true">·</span>
                <span className="text-emerald-400 font-semibold">
                  {EXPECTED_TNS_VERSION} ({liveBinCheck?.buildStamp ?? EXPECTED_TNS_BUILD_STAMP})
                </span>
                <span aria-hidden="true">·</span>
                <span>Ndless SDK (PRG\0 + Zehn v1 @ 0x1ec)</span>
                <span aria-hidden="true">·</span>
                <span>
                  {(liveBinCheck?.sizeBytes ?? EXPECTED_TNS_SIZE_BYTES).toLocaleString('fr-FR')} octets · SHA-256{' '}
                  {(liveBinCheck?.sha256Hex ?? EXPECTED_TNS_SHA256).slice(0, 12)}…
                </span>
              </div>
              <h1 className="text-xl sm:text-2xl font-bold text-slate-100 tracking-tight">
                N-Launcher.tns — Exécutable Natif & Simulateur Console
              </h1>
            </div>

            <div className="flex flex-wrap items-center gap-2.5 shrink-0">
              <button
                type="button"
                onClick={handleDownloadBinary}
                className="inline-flex items-center gap-2 rounded-lg bg-emerald-500 px-4 py-2 text-xs font-semibold text-slate-950 hover:bg-emerald-400 transition-colors whitespace-nowrap shrink-0"
              >
                <Download className="h-4 w-4" />
                N-Launcher.tns ({EXPECTED_TNS_SIZE_BYTES.toLocaleString('fr-FR')} octets · #R7)
              </button>
              <button
                type="button"
                onClick={() => setActiveSection('binary')}
                className="inline-flex items-center gap-1.5 rounded-lg border border-slate-700 bg-slate-800 px-3.5 py-2 text-xs font-medium text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
              >
                <Cpu className="h-3.5 w-3.5 text-sky-400" />
                Audit Binaire & Tests C
              </button>
              <button
                type="button"
                onClick={() => setActiveSection('source')}
                className="inline-flex items-center gap-1.5 rounded-lg border border-slate-700 bg-slate-800 px-3.5 py-2 text-xs font-medium text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
              >
                Code Source Modulaire C
              </button>
            </div>
          </div>

          {downloadBanner && (
            <div className="rounded border border-emerald-500/40 bg-emerald-500/10 px-3.5 py-2 text-xs font-mono text-emerald-300 flex items-center gap-2">
              <CheckCircle2 className="h-4 w-4 shrink-0" />
              <span>{downloadBanner}</span>
            </div>
          )}
        </section>

        {activeSection === 'simulator' && (
          <div className="grid grid-cols-1 lg:grid-cols-12 gap-6 items-start">
            {/* Left 7 Cols: 320x240 Framebuffer Display + Physical Keypad Controls */}
            <div className="lg:col-span-7 rounded-lg border border-slate-800 bg-slate-900/90 p-5 space-y-5">
              <div className="flex flex-wrap items-center justify-between gap-3 border-b border-slate-800 pb-3">
                <div>
                  <h2 className="text-sm font-semibold text-slate-100">
                    Rendu Natif TI-Nspire CX II-T (320 × 240 RGB565)
                  </h2>
                  <p className="text-xs text-slate-400 mt-0.5">
                    Grille {LAYOUT_LABELS[state.settings.layout]} · Tri {SORT_ORDER_LABELS[state.settings.sortOrder]} · v3.1
                  </p>
                </div>

                <div className="flex items-center gap-2">
                  <button
                    type="button"
                    onClick={() => {
                      const saved = loadSavedPrefs();
                      setState((prev) => {
                        const fallbackPrefs = saved ?? {
                          profiles: snapshotActiveProfile(prev).profiles,
                        };
                        return scanVirtualFilesystem(
                          virtualFiles,
                          '/documents/ndless/N-Launcher.tns',
                          prev.games,
                          prev.playCounter,
                          fallbackPrefs
                        );
                      });
                      setBootSeq((n) => n + 1);
                    }}
                    className="inline-flex items-center gap-1 rounded border border-slate-700 bg-slate-800 px-2.5 py-1 text-xs font-mono text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    title="Relancer l’animation de démarrage avec les préférences sauvegardées"
                  >
                    <RotateCcw className="h-3 w-3 text-sky-400" />
                    Démarrage
                  </button>

                  <div className="flex items-center rounded border border-slate-800 bg-slate-950 p-0.5 text-xs font-mono">
                    <button
                      type="button"
                      onClick={() => setCanvasScale(1)}
                      className={`px-2.5 py-1 rounded transition-colors whitespace-nowrap shrink-0 ${
                        canvasScale === 1
                          ? 'bg-slate-800 text-slate-100 font-semibold'
                          : 'text-slate-400 hover:text-slate-200'
                      }`}
                    >
                      1× (320px)
                    </button>
                    <button
                      type="button"
                      onClick={() => setCanvasScale(2)}
                      className={`px-2.5 py-1 rounded transition-colors whitespace-nowrap shrink-0 ${
                        canvasScale === 2
                          ? 'bg-slate-800 text-slate-100 font-semibold'
                          : 'text-slate-400 hover:text-slate-200'
                      }`}
                    >
                      2× (640px)
                    </button>
                  </div>
                </div>
              </div>

              {/* Strict 3-Category Filter Bar (TOUS, FAV, REC) + Active Profile Quick Trigger */}
              <div className="flex flex-wrap items-center justify-between gap-2">
                <div className="flex flex-wrap items-center gap-2">
                  <button
                    type="button"
                    onClick={() => dispatchKey('PROFILE_QUICK')}
                    className="inline-flex items-center gap-2 rounded-lg border border-slate-800 bg-slate-950 px-2.5 py-1 text-xs font-mono text-slate-200 hover:border-sky-500/50 transition-colors whitespace-nowrap shrink-0"
                    title="Sélecteur rapide de profils (touche .)"
                  >
                    <img
                      src={getAvatarImageUrl(getActiveProfile(state).avatarId)}
                      alt={getActiveProfile(state).name}
                      className="h-4 w-4 rounded-full object-cover"
                    />
                    <span className="font-semibold text-sky-300">
                      {getActiveProfile(state).name}
                    </span>
                  </button>
                  <div className="flex items-center gap-1 rounded-lg bg-slate-950 p-1 border border-slate-800">
                    {CATEGORY_ORDER.map((cat) => (
                      <button
                        key={cat}
                        type="button"
                        onClick={() => handleSelectCategory(cat)}
                        className={`px-3 py-1 rounded text-xs font-mono font-medium transition-colors whitespace-nowrap shrink-0 ${
                          state.activeCategory === cat
                            ? 'bg-sky-500 text-slate-950 font-semibold'
                            : 'text-slate-400 hover:text-slate-200'
                        }`}
                      >
                        {CATEGORY_LABELS[cat]}
                      </button>
                    ))}
                  </div>
                </div>
                <span className="text-xs font-mono text-slate-400">
                  {state.filteredIndices.length} jeu(x) · Page {state.currentPage + 1}/{state.totalPages}
                </span>
              </div>

              <div className="flex justify-center overflow-x-auto py-1">
                <NspireScreenCanvas
                  ref={canvasRef}
                  state={state}
                  scale={canvasScale}
                  bootSeq={bootSeq}
                  onKeyCommand={dispatchKey}
                  onSelectSlot={handleSelectSlot}
                  onCaptureDuringGame={handleCaptureDuringGame}
                  onTouchpadControl={(
                    tpCursor,
                    sensitivity,
                    toggleNav,
                    cursorShow,
                    cursorStyle,
                    tpTab
                  ) =>
                    setState((prev) =>
                      setTouchpadControl(
                        prev,
                        tpCursor,
                        sensitivity,
                        toggleNav,
                        cursorShow,
                        cursorStyle,
                        tpTab
                      )
                    )
                  }
                />
              </div>

              {/* Physical TI-Nspire CX II-T Keypad Deck */}
              <div className="rounded-lg border border-slate-800 bg-slate-950/90 p-4">
                <div className="text-xs font-medium text-slate-400 mb-3 flex flex-wrap items-center justify-between gap-2">
                  <span>Pavé Physique TI-Nspire CX II-T</span>
                  <span className="font-mono text-slate-500">
                    Clavier A-Z : Recherche · ENTER/5 : Lancer · Ctrl+S : Param. · Tab
                  </span>
                </div>

                <div className="grid grid-cols-1 sm:grid-cols-12 gap-4 items-center">
                  <div className="sm:col-span-5 grid grid-cols-2 gap-2">
                    <button
                      type="button"
                      onClick={() => dispatchKey('ESC')}
                      className="flex items-center justify-center gap-1.5 rounded border border-rose-500/40 bg-rose-500/10 px-3 py-2 text-xs font-mono font-semibold text-rose-300 hover:bg-rose-500/20 transition-colors whitespace-nowrap shrink-0"
                    >
                      ESC (Quitter)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('TAB')}
                      className="flex items-center justify-center gap-1.5 rounded border border-slate-700 bg-slate-800 px-3 py-2 text-xs font-mono font-semibold text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      TAB (Cat.)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('DOCK')}
                      className="flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800/80 px-2.5 py-2 text-xs font-mono text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      DOC (Mode)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('MENU')}
                      className="flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800/80 px-2.5 py-2 text-xs font-mono text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      MENU (Couleur)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('FAV')}
                      className="col-span-2 flex items-center justify-center gap-1.5 rounded border border-amber-500/40 bg-amber-500/10 px-3 py-2 text-xs font-mono font-semibold text-amber-300 hover:bg-amber-500/20 transition-colors whitespace-nowrap shrink-0"
                    >
                      <Star className="h-3.5 w-3.5" />
                      VAR (Favori)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('PAGE_PREV')}
                      className="flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800/80 px-2.5 py-2 text-xs font-mono text-slate-300 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      TRIG (Page -)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('PAGE_NEXT')}
                      className="flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800/80 px-2.5 py-2 text-xs font-mono text-slate-300 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      = (Page +)
                    </button>
                    <div className="col-span-2 grid grid-cols-3 gap-1.5">
                      <button
                        type="button"
                        onClick={() => dispatchKey('PROFILE_PREV')}
                        className="flex items-center justify-center rounded border border-slate-700 bg-slate-800/80 px-2 py-1.5 text-[11px] font-mono text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap"
                        title="Profil précédent (touche 0)"
                      >
                        0 (Préc.)
                      </button>
                      <button
                        type="button"
                        onClick={() => dispatchKey('PROFILE_QUICK')}
                        className="flex items-center justify-center rounded border border-sky-500/40 bg-sky-500/10 px-2 py-1.5 text-[11px] font-mono font-semibold text-sky-300 hover:bg-sky-500/20 transition-colors whitespace-nowrap"
                        title="Sélecteur rapide de profils (touche .)"
                      >
                        . (Profils)
                      </button>
                      <button
                        type="button"
                        onClick={() => dispatchKey('PROFILE_NEXT')}
                        className="flex items-center justify-center rounded border border-slate-700 bg-slate-800/80 px-2 py-1.5 text-[11px] font-mono text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap"
                        title="Profil suivant (touche -)"
                      >
                        - (Suiv.)
                      </button>
                    </div>
                  </div>

                  <div className="sm:col-span-4 flex flex-col items-center gap-1.5">
                    <button
                      type="button"
                      onClick={() => dispatchKey('UP')}
                      aria-label="Flèche haut"
                      className="w-24 flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800 py-2 text-xs font-mono font-semibold text-slate-100 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      <ArrowUp className="h-3.5 w-3.5" />
                      HAUT
                    </button>
                    <div className="flex items-center gap-1.5">
                      <button
                        type="button"
                        onClick={() => dispatchKey('LEFT')}
                        aria-label="Flèche gauche"
                        className="w-20 flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800 py-2 text-xs font-mono font-semibold text-slate-100 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                      >
                        <ArrowLeft className="h-3.5 w-3.5" />
                        GAUCHE
                      </button>
                      <div
                        role="region"
                        aria-label="Surface du pavé tactile TI-Nspire (clic centre pour lancer, glisser pour naviguer)"
                        data-testid="nspire-touchpad-surface"
                        onPointerDown={(e) => {
                          e.currentTarget.setPointerCapture(e.pointerId);
                          tpDraggingRef.current = true;
                          const rect = e.currentTarget.getBoundingClientRect();
                          const rx = Math.round(((e.clientX - rect.left) / Math.max(1, rect.width)) * 2328);
                          const ry = Math.round((1 - (e.clientY - rect.top) / Math.max(1, rect.height)) * 1691);
                          resetTouchpadTracker(tpTrackerRef.current);
                          resetTouchpadClickTracker(tpClickRef.current);
                          processTouchpadClick(tpClickRef.current, true, false, true, rx, ry, 2328, 1691);
                          processTouchpadSample(
                            tpTrackerRef.current,
                            stateRef.current.settings.touchpadNav,
                            true,
                            rx,
                            ry,
                            2328,
                            1691,
                            true,
                            stateRef.current.settings.touchpadSensitivity
                          );
                        }}
                        onPointerMove={(e) => {
                          if (!tpDraggingRef.current && e.buttons === 0) return;
                          const rect = e.currentTarget.getBoundingClientRect();
                          const rx = Math.round(((e.clientX - rect.left) / Math.max(1, rect.width)) * 2328);
                          const ry = Math.round((1 - (e.clientY - rect.top) / Math.max(1, rect.height)) * 1691);
                          processTouchpadClick(tpClickRef.current, true, false, true, rx, ry, 2328, 1691);
                          const cur = stateRef.current;
                          if (!cur.settings.touchpadNav) return;
                          if (tpTrackerRef.current.cooldownTicks > 0) {
                            tpTrackerRef.current.cooldownTicks = 1;
                          }
                          const navKey = processTouchpadSample(
                            tpTrackerRef.current,
                            true,
                            true,
                            rx,
                            ry,
                            2328,
                            1691,
                            true,
                            cur.settings.touchpadSensitivity
                          );
                          if (navKey) {
                            dispatchKey(navKey);
                          }
                        }}
                        onPointerUp={(e) => {
                          const rect = e.currentTarget.getBoundingClientRect();
                          const rx = Math.round(((e.clientX - rect.left) / Math.max(1, rect.width)) * 2328);
                          const ry = Math.round((1 - (e.clientY - rect.top) / Math.max(1, rect.height)) * 1691);
                          const cur = stateRef.current;
                          const shouldLaunch =
                            cur.sui.screen === 'LIBRARY' &&
                            cur.runningGameIndex === null &&
                            processTouchpadClick(tpClickRef.current, true, true, true, rx, ry, 2328, 1691);
                          tpDraggingRef.current = false;
                          resetTouchpadTracker(tpTrackerRef.current);
                          resetTouchpadClickTracker(tpClickRef.current);
                          if (shouldLaunch) {
                            dispatchKey('EXE');
                          }
                        }}
                        onPointerCancel={() => {
                          tpDraggingRef.current = false;
                          resetTouchpadTracker(tpTrackerRef.current);
                          resetTouchpadClickTracker(tpClickRef.current);
                        }}
                        title={
                          state.settings.touchpadNav
                            ? 'Pavé tactile ACTIF : cliquez au centre pour lancer ou glissez pour naviguer'
                            : 'Pavé tactile : cliquez au centre pour lancer (activez « Navigation tactile » pour glisser)'
                        }
                        className={`w-16 h-10 select-none touch-none flex flex-col items-center justify-center rounded border text-[10px] font-mono leading-tight transition-colors cursor-pointer ${
                          state.settings.touchpadNav
                            ? 'border-emerald-500/60 bg-emerald-500/15 text-emerald-300 active:cursor-grabbing'
                            : 'border-sky-500/40 bg-slate-900 text-slate-300 hover:bg-slate-800'
                        }`}
                      >
                        <span className="font-bold">TACTILE</span>
                        <span className="text-[9px] opacity-80">
                          {state.settings.touchpadNav ? 'GLIS/CLIC' : 'CLIC OK'}
                        </span>
                      </div>
                      <button
                        type="button"
                        onClick={() => dispatchKey('RIGHT')}
                        aria-label="Flèche droite"
                        className="w-20 flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800 py-2 text-xs font-mono font-semibold text-slate-100 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                      >
                        DROITE
                        <ArrowRight className="h-3.5 w-3.5" />
                      </button>
                    </div>
                    <button
                      type="button"
                      onClick={() => dispatchKey('DOWN')}
                      aria-label="Flèche bas"
                      className="w-24 flex items-center justify-center gap-1 rounded border border-slate-700 bg-slate-800 py-2 text-xs font-mono font-semibold text-slate-100 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      <ArrowDown className="h-3.5 w-3.5" />
                      BAS
                    </button>
                  </div>

                  <div className="sm:col-span-3 flex flex-col gap-2">
                    <div className="grid grid-cols-2 gap-1.5">
                      <button
                        type="button"
                        onPointerDown={() => {
                          if (
                            stateRef.current.sui.screen === 'LIBRARY' &&
                            stateRef.current.runningGameIndex === null
                          ) {
                            setState((prev) => ({ ...prev, launchHolding: true }));
                          }
                        }}
                        onPointerUp={() => dispatchKey('EXE')}
                        onPointerCancel={() =>
                          setState((prev) => ({ ...prev, launchHolding: false }))
                        }
                        className="flex items-center justify-center gap-1 rounded-lg bg-sky-500 px-2.5 py-2.5 text-xs font-mono font-bold text-slate-950 hover:bg-sky-400 transition-colors whitespace-nowrap shrink-0"
                      >
                        <Play className="h-3.5 w-3.5 fill-current" />
                        ENTER
                      </button>
                      <button
                        type="button"
                        onPointerDown={() => {
                          if (
                            stateRef.current.sui.screen === 'LIBRARY' &&
                            stateRef.current.runningGameIndex === null
                          ) {
                            setState((prev) => ({ ...prev, launchHolding: true }));
                          }
                        }}
                        onPointerUp={() => dispatchKey('EXE')}
                        onPointerCancel={() =>
                          setState((prev) => ({ ...prev, launchHolding: false }))
                        }
                        className="flex items-center justify-center gap-1 rounded-lg border border-sky-400/50 bg-sky-500/20 px-2.5 py-2.5 text-xs font-mono font-bold text-sky-300 hover:bg-sky-500/30 transition-colors whitespace-nowrap shrink-0"
                        title="Touche 5 : Lancer le jeu sélectionné"
                      >
                        <Play className="h-3.5 w-3.5 fill-current" />5 (Jouer)
                      </button>
                    </div>
                    <button
                      type="button"
                      onClick={() => dispatchKey('RESCAN')}
                      className="w-full flex items-center justify-center gap-1.5 rounded border border-slate-700 bg-slate-800 px-3 py-2 text-xs font-mono text-slate-300 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                    >
                      <RotateCcw className="h-3.5 w-3.5" />
                      DEL (Rescan)
                    </button>
                    <button
                      type="button"
                      onClick={() => dispatchKey('SETTINGS')}
                      className="w-full flex items-center justify-center gap-1.5 rounded border border-sky-500/40 bg-sky-500/10 px-3 py-2 text-xs font-mono font-semibold text-sky-300 hover:bg-sky-500/20 transition-colors whitespace-nowrap shrink-0"
                    >
                      <Settings className="h-3.5 w-3.5" />
                      SAVE (Param.)
                    </button>
                  </div>
                </div>
              </div>
            </div>

            {/* Right 5 Cols: Selected Game Details & Binary Scanner Summary */}
            <div className="lg:col-span-5 space-y-5">
              <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5 space-y-4">
                <div className="flex items-center justify-between border-b border-slate-800 pb-3">
                  <h2 className="text-sm font-semibold text-slate-100">
                    Jeu Sélectionné (Slot {(state.selectedPos % getLayoutMetrics(state.settings.layout, state.settings.showNames).perPage) + 1}/{getLayoutMetrics(state.settings.layout, state.settings.showNames).perPage})
                  </h2>
                  <span className="text-xs font-mono text-sky-400">
                    {state.selectedPos + 1} / {state.filteredIndices.length}
                  </span>
                </div>

                {selectedGame ? (
                  <div className="space-y-3 text-xs font-mono">
                    <div className="flex items-center justify-between">
                      <div>
                        <div className="text-base font-bold font-sans text-slate-100 flex items-center gap-2">
                          <span>{selectedGame.title}</span>
                          {selectedGame.isFavorite && (
                            <span className="text-amber-400 text-xs font-mono">★ Favori</span>
                          )}
                          {selectedGame.shortcutKey && (
                            <span className="rounded bg-sky-500/20 border border-sky-500/40 px-1.5 py-0.5 text-[11px] font-mono text-sky-300">
                              [{selectedGame.shortcutKey}]
                            </span>
                          )}
                        </div>
                        <div className="text-slate-400 mt-0.5">{selectedGame.path}</div>
                      </div>
                      <span className="rounded border border-emerald-500/40 bg-emerald-500/10 px-2.5 py-1 text-emerald-300 font-semibold">
                        {selectedGame.binFormat} v{selectedGame.execVersion}
                      </span>
                    </div>

                    <div className="grid grid-cols-2 gap-2.5 pt-1">
                      <div className="rounded bg-slate-950/80 p-2.5">
                        <div className="text-slate-400">Auteur (Zehn)</div>
                        <div className="mt-0.5 text-slate-100 font-semibold">
                          {selectedGame.author}
                        </div>
                      </div>
                      <div className="rounded bg-slate-950/80 p-2.5">
                        <div className="text-slate-400">Taille / Relocs</div>
                        <div className="mt-0.5 text-slate-100 font-semibold">
                          {(selectedGame.sizeBytes / 1024).toFixed(1)} Ko · {selectedGame.relocCount}
                        </div>
                      </div>
                      <div className="rounded bg-slate-950/80 p-2.5">
                        <div className="text-slate-400">Statistiques</div>
                        <div className="mt-0.5 text-emerald-400 font-semibold">
                          {selectedGame.launchCount} lanc. · {formatDuration(selectedGame.playTimeSec)}
                        </div>
                      </div>
                      <div className="rounded bg-slate-950/80 p-2.5">
                        <div className="text-slate-400">Dernière session</div>
                        <div className="mt-0.5 text-sky-400 font-semibold">
                          {formatTimestamp(selectedGame.lastPlayedTs)}
                        </div>
                      </div>
                    </div>

                    <div className="rounded border border-slate-800 bg-slate-950/80 p-2.5 flex items-center justify-between gap-2 text-[11px]">
                      <span className="text-slate-400">
                        Lancement manuel · Ndless{' '}
                        <span className={state.sys.ndlessPersistent ? 'text-emerald-400' : 'text-amber-400'}>
                          {state.sys.ndlessPersistent ? 'Persistant' : 'Classique'}
                        </span>
                      </span>
                      <button
                        type="button"
                        onClick={() => setState((prev) => toggleSimulatedNdlessPersistence(prev))}
                        className="rounded border border-slate-700 bg-slate-800 px-2 py-1 text-[10px] font-sans text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                      >
                        Simuler Ndless {state.sys.ndlessPersistent ? 'Classique' : 'Persist.'}
                      </button>
                    </div>

                    <div className="rounded border border-slate-800 bg-slate-950/80 p-3 flex items-center justify-between gap-3">
                      <div className="space-y-1">
                        <div className="text-slate-300 font-semibold flex items-center gap-1.5">
                          <Camera className="h-3.5 w-3.5 text-sky-400" />
                          Icône & Capture de Lancement (94 × 68 RGB565)
                        </div>
                        <div className="text-slate-400 text-[11px]">
                          {selectedGame.iconSource === 'ICO'
                            ? 'Icône personnalisée (.ico.tns)'
                            : selectedGame.iconSource === 'BMP'
                            ? 'Icône personnalisée (.bmp.tns)'
                            : selectedGame.iconSource === 'CAPTURED'
                            ? 'Icône capturée (.bmp.tns)'
                            : 'Emblème par défaut'}{' '}
                          ·{' '}
                          <span
                            className={
                              selectedGame.hasScreenshot ? 'text-emerald-400' : 'text-amber-400'
                            }
                          >
                            {selectedGame.hasScreenshot
                              ? 'Capture réelle (.scr.tns) prête pour le fondu'
                              : 'Capture .scr.tns en attente'}
                          </span>
                        </div>
                      </div>
                      <label className="inline-flex items-center gap-1.5 rounded border border-slate-700 bg-slate-800 px-2.5 py-1.5 text-[11px] font-sans font-medium text-slate-200 hover:bg-slate-700 cursor-pointer transition-colors whitespace-nowrap shrink-0">
                        <ImagePlus className="h-3.5 w-3.5 text-sky-400" />
                        .ico / .bmp
                        <input
                          type="file"
                          accept=".ico,.ico.tns,.bmp,.bmp.tns,image/*"
                          onChange={handleUploadCustomCover}
                          className="hidden"
                        />
                      </label>
                    </div>

                    <div className="flex items-center gap-2 pt-1">
                      <button
                        type="button"
                        onClick={() => dispatchKey('EXE')}
                        className="flex-1 inline-flex items-center justify-center gap-1.5 rounded-lg bg-sky-500 px-3 py-2 text-xs font-sans font-semibold text-slate-950 hover:bg-sky-400 transition-colors whitespace-nowrap shrink-0"
                      >
                        <Play className="h-3.5 w-3.5 fill-current" />
                        Jouer (EXE)
                      </button>
                      <button
                        type="button"
                        onClick={() => dispatchKey('FAV')}
                        className="inline-flex items-center justify-center gap-1.5 rounded-lg border border-slate-700 bg-slate-800 px-3 py-2 text-xs font-sans font-medium text-slate-200 hover:bg-slate-700 transition-colors whitespace-nowrap shrink-0"
                      >
                        <Star className="h-3.5 w-3.5 text-amber-400" />
                        {selectedGame.isFavorite ? 'Retirer Favori' : 'Ajouter Favori'}
                      </button>
                    </div>
                  </div>
                ) : (
                  <p className="text-xs text-slate-400">
                    Aucun jeu dans cette catégorie. Appuyez sur TAB ou ESC pour revenir à TOUS.
                  </p>
                )}
              </div>

              <div className="rounded-lg border border-slate-800 bg-slate-900/90 p-5 space-y-3">
                <div className="flex items-center justify-between">
                  <h3 className="text-sm font-semibold text-slate-100 flex items-center gap-2">
                    <FolderSearch className="h-4 w-4 text-emerald-400" />
                    Filtrage Binaire Automatique (<code className="text-emerald-400">.tns</code>)
                  </h3>
                  <button
                    type="button"
                    onClick={() => setActiveSection('filesystem')}
                    className="text-xs font-medium text-sky-400 hover:text-sky-300 transition-colors whitespace-nowrap shrink-0"
                  >
                    Tester l’analyseur →
                  </button>
                </div>

                <p className="text-xs text-slate-400 leading-relaxed">
                  <strong>N-Launcher</strong> inspecte l’en-tête binaire de chaque fichier{' '}
                  <code>.tns</code> pour isoler les jeux Ndless (<code>PRG\0</code>,{' '}
                  <code>Zehn</code>, <code>bFLT</code>) et ignorer les cours TI-Nspire (
                  <code>PK\x03\x04</code>) ainsi que les jaquettes.
                </p>

                <div className="grid grid-cols-4 gap-2 text-center font-mono text-xs pt-1">
                  <div className="rounded bg-slate-950/80 p-2">
                    <div className="text-emerald-400 font-bold text-sm">
                      {state.stats.acceptedGames}
                    </div>
                    <div className="text-slate-400 text-[11px]">Jeux</div>
                  </div>
                  <div className="rounded bg-slate-950/80 p-2">
                    <div className="text-amber-400 font-bold text-sm">
                      {state.stats.rejectedDocs}
                    </div>
                    <div className="text-slate-400 text-[11px]">Docs TI</div>
                  </div>
                  <div className="rounded bg-slate-950/80 p-2">
                    <div className="text-sky-400 font-bold text-sm">{state.stats.rejectedSys}</div>
                    <div className="text-slate-400 text-[11px]">Système</div>
                  </div>
                  <div className="rounded bg-slate-950/80 p-2">
                    <div className="text-rose-400 font-bold text-sm">
                      {state.stats.rejectedDups}
                    </div>
                    <div className="text-slate-400 text-[11px]">Doublon</div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        )}

        {activeSection === 'filesystem' && (
          <FilesystemInspectorPanel
            state={state}
            onAddVirtualFile={handleAddVirtualFile}
            onResetFilesystem={handleResetFilesystem}
          />
        )}

        {activeSection === 'binary' && <SourceAndDeliverablesPanel mode="binary" />}

        {activeSection === 'source' && <SourceAndDeliverablesPanel mode="source" />}
      </main>

      <footer className="border-t border-slate-800/80 bg-slate-950 px-6 py-4 text-xs text-slate-400">
        <div className="max-w-7xl mx-auto flex flex-col sm:flex-row items-center justify-between gap-2">
          <span>
            N-Launcher · C11 Modulaire / Ndless SDK (ARM926EJ-S · TI-Nspire CX II-T · 320 × 240 RGB565)
          </span>
          <div className="flex items-center gap-4">
            <button
              type="button"
              onClick={handleDownloadBinary}
              className="text-sky-400 hover:underline whitespace-nowrap"
            >
              Télécharger N-Launcher.tns
            </button>
            <span aria-hidden="true">·</span>
            <button
              type="button"
              onClick={() => downloadDeliverableBlob('/deliverables/PROGRESS.md', 'PROGRESS.md')}
              className="hover:text-slate-200 whitespace-nowrap"
            >
              PROGRESS.md
            </button>
            <span aria-hidden="true">·</span>
            <button
              type="button"
              onClick={() => downloadDeliverableBlob('/deliverables/README.md', 'README.md')}
              className="hover:text-slate-200 whitespace-nowrap"
            >
              README.md
            </button>
            <span aria-hidden="true">·</span>
            <button
              type="button"
              onClick={() => downloadDeliverableBlob('/deliverables/TEST_REPORT.md', 'TEST_REPORT.md')}
              className="hover:text-slate-200 whitespace-nowrap"
            >
              TEST_REPORT.md
            </button>
          </div>
        </div>
      </footer>
    </div>
  );
}
