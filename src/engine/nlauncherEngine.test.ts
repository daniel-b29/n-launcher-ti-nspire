import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {
  ACCENT_COLOR_ORDER,
  CATEGORY_LABELS,
  CATEGORY_ORDER,
  CURSOR_SIZE_LABELS,
  CURSOR_SIZE_ORDER,
  DISPLAY_MODE_ORDER,
  LAYOUT_ORDER,
  NL_COVER_RADIUS,
  NL_CURSOR_SIZE_DEFAULT,
  NL_DISPLAY_OPT_COUNT,
  NL_GAMES_PER_PAGE,
  NL_GRID_CARD_H,
  NL_GRID_CARD_W,
  NL_GRID_COLS,
  NL_GRID_GAP_X,
  NL_GRID_MARGIN_X,
  NL_GRID_ROWS,
  NL_ICON_H,
  NL_ICON_MAX_DIM,
  NL_ICON_W,
  NL_PAGE_STRIDE_Y,
  NL_PAGINATION_MAX_DOTS,
  NL_TP_CLICK_SLIDE_CANCEL,
  NL_TP_SENS_COUNT,
  NL_TP_SENS_DEFAULT,
  NL_TP_THRESHOLDS,
  NL_VIEW_BOTTOM,
  SETTINGS_MENU_ITEMS,
  SORT_ORDER_LABELS,
  SORT_ORDER_LIST,
  TP_SENS_LABELS,
  applyCapturedGameIcon,
  applyCapturedScreenshot,
  buildTiDocZipHeader,
  buildWindowsIcoBuffer,
  buildZehnTnsHeader,
  clampCursorSize,
  clampTouchpadSensitivity,
  computeGameMonogram,
  computeNavHighlightStrength,
  computePaginationIndicators,
  createInitialVirtualFilesystem,
  createSampleCustomIcoFiles,
  createTouchpadClickTracker,
  createTouchpadTracker,
  decodeWindowsIcoBuffer,
  extractGameStem,
  formatDuration,
  formatTimestamp,
  getConsoleTheme,
  getCursorSizeMetrics,
  getLayoutMetrics,
  getShortcutDefs,
  getTouchpadTab0RowRects,
  getTouchpadThresholdForLevel,
  runSimulatorRescan,
  handleCharInput,
  handleLauncherKey,
  inspectTnsBuffer,
  isShortcutAssignable,
  isShortcutReserved,
  isSystemOrSelfFile,
  NL_SCALE_NORMAL,
  NL_SCALE_SELECTED,
  NL_SCREEN_H,
  NL_SCREEN_W,
  ASSIGNABLE_SHORTCUT_KEYS,
  AVATAR_CATEGORIES,
  NL_AVATAR_CAT_COUNT,
  NL_AVATAR_COUNT,
  NL_MAX_PROFILES,
  NL_PROFILE_NAME_MAX,
  buildPngIcoBuffer,
  computeGridSlotPos,
  computeLibraryCacheCrc32,
  createDefaultSimulatorFilesystem,
  createUserProfile,
  deleteUserProfile,
  findAvatarOwner,
  firstFreeAvatar,
  getActiveProfile,
  getAvatarCategoryOf,
  getAvatarCategoryRange,
  getAvatarImageUrl,
  processTouchpadClick,
  processTouchpadSample,
  renameUserProfile,
  CURSOR_SPEED_LABELS,
  CURSOR_STYLE_LABELS,
  CURSOR_STYLE_ORDER,
  NL_CURSOR_SPEED_COUNT,
  NL_CURSOR_SPEED_DEFAULT,
  TOUCHPAD_MODE_LABELS,
  clampCursorSpeed,
  clampCursorStyle,
  clickMouse,
  computeTouchpadCursorTarget,
  feedMouseTouchpadDelta,
  isMouseModeActive,
  isTouchpadCursorVisible,
  moveMouseTo,
  resetSettingsOnly,
  resetTouchpadTracker,
  scanVirtualFilesystem,
  setCtrlMouseActive,
  isProfileProtected,
  setTouchpadControl,
  setUserProfileAvatar,
  stepProfile,
  switchProfileTo,
  toggleProfileLock,
  toggleSimulatedNdlessPersistence,
  updateMouseHover,
  NL_BOOT_STAGE_COUNT,
  clearDiagLogs,
  getBootStageLabel,
  getDiagDefaults,
  recordBootStage,
  recoverMemoryOnOom,
  reportDiagError,
  setHardwarePreset,
  simulateFaultInjection,
} from './nlauncherEngine';
import { verifyTnsBuffer } from '../utils/downloadTns';

test('v3 grid & rounded square card constants match 3x2 vertical scroll with bottom peek', () => {
  assert.equal(NL_GRID_COLS, 3);
  assert.equal(NL_GRID_ROWS, 2);
  assert.equal(NL_GAMES_PER_PAGE, 6);
  assert.equal(NL_GRID_CARD_W, 84);
  assert.equal(NL_GRID_CARD_H, 84);
  assert.equal(NL_GRID_MARGIN_X, 18);
  assert.equal(NL_GRID_GAP_X, 16);
  assert.equal(NL_ICON_W, 94);
  assert.equal(NL_ICON_H, 68);
  assert.equal(NL_ICON_MAX_DIM, 84);
  assert.equal(NL_COVER_RADIUS, 11);
  assert.equal(NL_PAGE_STRIDE_Y, 180);
  assert.equal(NL_PAGINATION_MAX_DOTS, 19);
  assert.equal(NL_VIEW_BOTTOM, 205);
  assert.deepEqual(CATEGORY_ORDER, ['ALL', 'FAVORITES', 'RECENT']);
  assert.deepEqual(
    CATEGORY_ORDER.map((c) => CATEGORY_LABELS[c]),
    ['TOUS', 'FAV', 'REC']
  );
});

test('subtle navigation highlight tracks scale smoothly and is strictly 0 during launch zoom', () => {
  assert.equal(computeNavHighlightStrength(NL_SCALE_NORMAL, false), 0);
  const midScale = (NL_SCALE_NORMAL + NL_SCALE_SELECTED) / 2;
  const midStrength = computeNavHighlightStrength(midScale, false);
  assert.equal(midStrength > 0.45 && midStrength < 0.55, true);
  assert.equal(computeNavHighlightStrength(NL_SCALE_SELECTED, false), 1);

  // Strictly suppressed during EXE launch validation / zoom
  assert.equal(computeNavHighlightStrength(NL_SCALE_SELECTED, true), 0);
  assert.equal(computeNavHighlightStrength(midScale, true), 0);
});

test('inspectTnsBuffer distinguishes Zehn/PRG executables from TI-Nspire ZIP documents, system files, and covers', () => {
  const zehnBuf = buildZehnTnsHeader({
    execName: 'nDoom',
    execAuthor: 'Mrakoplaz',
    version: 3,
    relocCount: 20,
    allocSize: 524288,
    totalSize: 145408,
    usesLcdBlit: true,
    supportsHww: true,
  });

  const validRes = inspectTnsBuffer(zehnBuf, 145408, '/documents/games/nDoom.tns');
  assert.equal(validRes.isExecutable, true);
  assert.equal(validRes.format, 'ZEHN');
  assert.equal(validRes.execName, 'nDoom');
  assert.equal(validRes.execAuthor, 'Mrakoplaz');
  assert.equal(validRes.usesLcdBlit, true);
  assert.equal(validRes.supportsHww, true);

  const tiDocBuf = buildTiDocZipHeader();
  const docRes = inspectTnsBuffer(tiDocBuf, 18432, '/documents/Lycee/Maths_Suites.tns');
  assert.equal(docRes.isExecutable, false);
  assert.equal(docRes.format, 'TI-DOC');

  const selfRes = inspectTnsBuffer(zehnBuf, 104164, '/documents/ndless/N-Launcher.tns');
  assert.equal(selfRes.isExecutable, false);
  assert.equal(selfRes.format, 'SYSTEM');

  const sysRes = inspectTnsBuffer(zehnBuf, 54812, '/documents/ndless/ndless_resources.tns');
  assert.equal(sysRes.isExecutable, false);
  assert.equal(sysRes.format, 'SYSTEM');

  const icoRes = inspectTnsBuffer(zehnBuf, 12800, '/documents/ndless/covers/nDoom.ico.tns');
  assert.equal(icoRes.isExecutable, false);
  assert.equal(icoRes.format, 'SYSTEM');

  const bmpRes = inspectTnsBuffer(zehnBuf, 19200, '/documents/ndless/covers/nDoom.bmp.tns');
  assert.equal(bmpRes.isExecutable, false);
  assert.equal(bmpRes.format, 'SYSTEM');
});

test('scanVirtualFilesystem starts clean, captures mid-game without exiting, and never overwrites existing covers', () => {
  const files = createInitialVirtualFilesystem();
  let state = scanVirtualFilesystem(files);

  assert.equal(state.stats.acceptedGames, 9);
  assert.equal(state.stats.rejectedDocs, 3);
  assert.equal(state.stats.rejectedSys, 3);
  assert.equal(state.stats.rejectedDups, 1);
  assert.equal(state.totalPages, 2);
  assert.equal(state.games.every((g) => g.iconSource === 'NONE'), true);
  assert.equal(computeGameMonogram('2048 CX'), '2C');

  // Launch game 0 first, then capture during active gameplay (preserveRunningState = true)
  state = handleLauncherKey(state, 'EXE');
  const launchedIdx = state.runningGameIndex;
  assert.equal(launchedIdx, 0);

  state = applyCapturedGameIcon(state, 0, 'data:image/png;base64,FIRSTCOVER==', 'CAPTURED', true);
  assert.equal(state.runningGameIndex, 0);
  assert.equal(state.games[0].iconSource, 'CAPTURED');
  assert.equal(state.toastMessage, 'JAQUETTE CAPTUREE !');

  // Subsequent automatic in-game capture must NEVER overwrite existing valid cover
  state = applyCapturedGameIcon(state, 0, 'data:image/png;base64,SECONDCOVER==', 'CAPTURED', true);
  assert.equal(state.runningGameIndex, 0);
  assert.equal(state.games[0].capturedIconDataUrl, 'data:image/png;base64,FIRSTCOVER==');

  // Exit game cleanly via ESC
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.runningGameIndex, null);

  // Survives rescan
  const rescanned = scanVirtualFilesystem(files, '/documents/ndless/N-Launcher.tns', state.games);
  assert.equal(rescanned.games[0].iconSource, 'CAPTURED');
  assert.equal(rescanned.games[0].capturedIconDataUrl, 'data:image/png;base64,FIRSTCOVER==');
});

test('handleLauncherKey navigates 3x2 grid, cycles 3 categories (TOUS/FAV/REC), launches & returns', () => {
  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.selectedPos, 0);
  assert.equal(state.currentPage, 0);

  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.selectedPos, 1);

  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.selectedPos, 4);

  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.selectedPos, 6);
  assert.equal(state.currentPage, 1);

  // Cycle categories strictly across TOUS -> FAV -> REC -> TOUS
  state = handleLauncherKey(state, 'TAB');
  assert.equal(state.activeCategory, 'FAVORITES');
  state = handleLauncherKey(state, 'TAB');
  assert.equal(state.activeCategory, 'RECENT');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.activeCategory, 'ALL');

  // Launch game via EXE and return via ESC
  const launchedGameIdx = state.filteredIndices[0];
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.runningGameIndex, launchedGameIdx);
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.runningGameIndex, null);
});

test('DOCK cycles dark -> light -> gray -> dark and MENU cycles 4 accents independently across all 12 themes', () => {
  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.displayMode, 'dark');
  assert.equal(state.accentColor, 'blue');
  assert.deepEqual(DISPLAY_MODE_ORDER, ['dark', 'light', 'gray']);

  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'TAB'); // FAV
  const prevPos = state.selectedPos;
  const prevPage = state.currentPage;
  const prevCat = state.activeCategory;
  const prevToastSeq = state.toastSeq;

  // DOCK cycles dark -> light -> gray -> dark without touching accent, selection, page, category or toast
  state = handleLauncherKey(state, 'DOCK');
  assert.equal(state.displayMode, 'light');
  assert.equal(state.accentColor, 'blue');
  assert.equal(state.selectedPos, prevPos);
  assert.equal(state.currentPage, prevPage);
  assert.equal(state.activeCategory, prevCat);
  assert.equal(state.toastSeq, prevToastSeq);
  assert.equal(state.toastMessage, null);

  state = handleLauncherKey(state, 'DOCK');
  assert.equal(state.displayMode, 'gray');
  assert.equal(state.accentColor, 'blue');
  assert.equal(state.selectedPos, prevPos);
  assert.equal(state.currentPage, prevPage);
  assert.equal(state.activeCategory, prevCat);
  assert.equal(state.toastSeq, prevToastSeq);

  // MENU cycles strictly Blue -> Red -> Green -> Yellow -> Blue while staying in gray mode
  const observed: string[] = [];
  for (let i = 0; i < 4; i++) {
    state = handleLauncherKey(state, 'MENU');
    observed.push(state.accentColor);
    assert.equal(state.displayMode, 'gray');
    assert.equal(state.selectedPos, prevPos);
    assert.equal(state.currentPage, prevPage);
    assert.equal(state.activeCategory, prevCat);
    assert.equal(state.toastSeq, prevToastSeq);
    assert.equal(state.toastMessage, null);
  }
  assert.deepEqual(observed, ['red', 'green', 'yellow', 'blue']);

  // Cycle back gray -> dark while retaining chosen accent
  state = handleLauncherKey(state, 'MENU'); // red
  state = handleLauncherKey(state, 'DOCK'); // dark
  assert.equal(state.displayMode, 'dark');
  assert.equal(state.accentColor, 'red');

  // Verify all 12 combinations (3 modes x 4 accents) have distinct contrast and defined accents
  for (const mode of DISPLAY_MODE_ORDER) {
    for (const accent of ACCENT_COLOR_ORDER) {
      const t = getConsoleTheme(mode, accent);
      assert.equal(t.isLight, mode === 'light');
      assert.notEqual(t.bgMain, t.textPrimary);
      assert.notEqual(t.accentPrimary, t.dotInactive);
    }
  }
});

test('computePaginationIndicators renders circular 8x8 dots & 18x8 capsule up to 19 pages, then switches to compact horizontal dashes (>19)', () => {
  for (const totalPages of [1, 2, 3, 5, 9, 12, 15, 19]) {
    const pagesToCheck = new Set([0, 1, Math.floor(totalPages / 2), totalPages - 2, totalPages - 1]);
    for (const p of pagesToCheck) {
      if (p < 0 || p >= totalPages) continue;
      const items = computePaginationIndicators(p, totalPages);
      assert.equal(items.length, totalPages);

      const activeItems = items.filter((it) => it.isActive && it.pageIndex === p);
      assert.equal(activeItems.length, 1, `page ${p + 1}/${totalPages} must have active capsule`);
      assert.equal(activeItems[0].mode, 'dots');
      assert.equal(activeItems[0].width, 18);
      assert.equal(activeItems[0].height, 8);
      assert.equal(activeItems[0].radius, 4);

      for (let i = 0; i < items.length; i++) {
        assert.equal(items[i].mode, 'dots');
        assert.equal(items[i].height, 8);
        assert.equal(items[i].radius, 4);
        if (!items[i].isActive) {
          assert.equal(items[i].width, 8);
        }
        if (i > 0) {
          const gap = items[i].x - (items[i - 1].x + items[i - 1].width);
          assert.equal(gap, 6, `gap must be strictly 6px between dot ${i - 1} and ${i}`);
        }
      }
    }
  }

  // Adaptive compact horizontal dashes for > 19 pages (20..128)
  for (const totalPages of [20, 32, 64, 128]) {
    const pagesToCheck = new Set([0, Math.floor(totalPages / 2), totalPages - 1]);
    for (const p of pagesToCheck) {
      const items = computePaginationIndicators(p, totalPages);
      assert.equal(items.length, totalPages);
      const activeItems = items.filter((it) => it.isActive && it.pageIndex === p);
      assert.equal(activeItems.length, 1);
      assert.equal(activeItems[0].mode, 'dashes');
      assert.equal(activeItems[0].height >= 4, true);
      assert.equal(items[0].x >= 16, true);
      const last = items[items.length - 1];
      assert.equal(last.x + last.width <= 304, true);

      for (let i = 0; i < items.length; i++) {
        assert.equal(items[i].mode, 'dashes');
        assert.equal(items[i].width >= 1, true);
        if (i > 0) {
          const gap = items[i].x - (items[i - 1].x + items[i - 1].width);
          assert.equal(gap >= 1, true);
        }
      }
    }
  }

  // Mid-glide check: total width and gaps remain strictly constant with 0 overlap
  const glide = computePaginationIndicators(1, 5, 1.4);
  assert.equal(glide.length, 5);
  const sumWidths = glide.reduce((acc, it) => acc + it.width, 0);
  assert.equal(sumWidths, 4 * 8 + 18);
  for (let i = 1; i < glide.length; i++) {
    assert.equal(glide[i].x - (glide[i - 1].x + glide[i - 1].width), 6);
  }
});

test('cold boot scanVirtualFilesystem loads saved theme preferences immediately without resetting on rescan', () => {
  const files = createInitialVirtualFilesystem();
  const s1 = scanVirtualFilesystem(files, '/documents/ndless/N-Launcher.tns', [], 2, {
    displayMode: 'light',
    accentColor: 'yellow',
  });
  assert.equal(s1.displayMode, 'light');
  assert.equal(s1.accentColor, 'yellow');

  const s2 = scanVirtualFilesystem(files, '/documents/ndless/N-Launcher.tns', s1.games, s1.playCounter, {
    displayMode: s1.displayMode,
    accentColor: s1.accentColor,
  });
  assert.equal(s2.displayMode, 'light');
  assert.equal(s2.accentColor, 'yellow');
});

test('real compiled N-Launcher.tns v3.1 binary on disk is a valid ARM PRG+Zehn v1 executable', () => {
  const tnsPath = path.resolve(process.cwd(), 'N-Launcher.tns');
  assert.equal(fs.existsSync(tnsPath), true, 'N-Launcher.tns must exist at project root');

  const fileBuf = fs.readFileSync(tnsPath);
  const rawBytes = new Uint8Array(fileBuf);
  assert.equal(rawBytes.length > 100000, true, 'N-Launcher.tns v3.1 must be a full compiled binary');

  const selfCheck = inspectTnsBuffer(rawBytes, rawBytes.length, '/documents/ndless/N-Launcher.tns');
  assert.equal(selfCheck.format, 'SYSTEM');

  const binInfo = inspectTnsBuffer(rawBytes, rawBytes.length, '/documents/test_target.tns');
  assert.equal(binInfo.isExecutable, true);
  assert.equal(binInfo.format, 'ZEHN');
  assert.equal(binInfo.zehnOffset, 0x1ec);
  assert.equal(binInfo.zehnVersion, 1);
  assert.equal(binInfo.execAuthor, 'daniel.b');
  assert.equal(binInfo.execVersion, 3);
  assert.equal(binInfo.relocCount > 1000, true);
  assert.equal(binInfo.allocSize > 400000 && binInfo.allocSize < 650000, true);
  assert.equal(binInfo.usesLcdBlit, true);
  assert.equal(binInfo.supportsHww, true);

  const ab = fileBuf.buffer.slice(fileBuf.byteOffset, fileBuf.byteOffset + fileBuf.byteLength);
  const vOk = verifyTnsBuffer(ab, 'N-Launcher.tns');
  assert.equal(vOk.valid, true);
  assert.equal(vOk.outerMagicHex, '50 52 47 00');

  const vBadName = verifyTnsBuffer(ab, 'n-lancher .tns');
  assert.equal(vBadName.valid, false);
  assert.match(vBadName.diagnosis, /espace avant \.tns/);
});

test('v3.1 settings menu, layouts, showShortcuts expansion, touchpad nav, shortcuts, 7 sorts, search, stats & safe reset', () => {
  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.sui.screen, 'LIBRARY');
  assert.equal(state.settings.showShortcuts, true);
  assert.equal(state.settings.touchpadNav, false);
  assert.equal(state.settings.autostart, false);
  assert.equal(state.settings.showBattery, false);

  // Key '5' is reserved for game launch and never assignable as a custom game shortcut
  assert.equal(isShortcutReserved('5'), true);
  assert.equal(isShortcutAssignable('5'), false);

  // Pressing '5' on the Library screen launches the currently selected game
  state = handleLauncherKey(state, 'RIGHT'); // pos 1
  const expectedIdx = state.filteredIndices[1];
  state = handleCharInput(state, '5');
  assert.equal(state.runningGameIndex, expectedIdx);
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.runningGameIndex, null);

  // Open settings & return
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(state.sui.screen, 'SETTINGS_MAIN');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'LIBRARY');

  // Layout metrics bounds for all 5 layouts x showNames x showShortcuts (square 1:1 cards, tight fit)
  for (const lay of LAYOUT_ORDER) {
    for (const sn of [false, true]) {
      for (const sc of [true, false]) {
        const m = getLayoutMetrics(lay, sn, sc);
        assert.equal(m.cols >= 1 && m.rows >= 1, true);
        assert.equal(m.cardW, m.cardH, `${lay} (${sn}/${sc}) cards must be square 1:1`);
        assert.equal(m.cardW >= 24, true);
        assert.equal(m.marginX >= 10, true);
        assert.equal(m.topY >= 28, true);
        assert.equal(m.viewBottom, sc ? 205 : 222);
        assert.equal(m.paginationY, sc ? 209 : 226);
        const rightEdge = 2 * m.marginX + m.cols * m.cardW + (m.cols - 1) * m.gapX;
        assert.equal(rightEdge <= NL_SCREEN_W, true);
        const lastBottom =
          m.topY + (m.rows - 1) * m.rowStrideY + m.cardH + m.labelH;
        assert.equal(lastBottom <= m.viewBottom, true);
      }
    }
  }
  const def3x2 = getLayoutMetrics('3x2', false, true, 6);
  assert.equal(def3x2.cardW, 84);
  assert.equal(def3x2.cardH, 84);
  assert.equal(def3x2.radius, 11);

  const exp3x2 = getLayoutMetrics('3x2', false, false, 6);
  assert.equal(exp3x2.cardW, 90);
  assert.equal(exp3x2.cardH, 90);
  assert.equal(exp3x2.viewBottom, 222);
  assert.equal(exp3x2.paginationY, 226);
  const pagNoSc = computePaginationIndicators(0, 2, 0, exp3x2.paginationY);
  assert.equal(pagNoSc[0].y, 226);

  // Toggle showShortcuts in SETTINGS_DISPLAY (row 0 of SETTINGS_MAIN, option 3 of SETTINGS_DISPLAY)
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_DISPLAY');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_DISPLAY');
  assert.equal(state.sui.displayCursor, 0);
  state = handleLauncherKey(state, 'DOWN'); // row 1 (scrollDir)
  assert.equal(state.sui.displayCursor, 1);
  state = handleLauncherKey(state, 'DOWN'); // row 2 (showNames)
  assert.equal(state.sui.displayCursor, 2);
  state = handleLauncherKey(state, 'DOWN'); // row 3 (showShortcuts)
  assert.equal(state.sui.displayCursor, 3);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.showShortcuts, false);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.showShortcuts, true);
  state = handleLauncherKey(state, 'ESC'); // back to SETTINGS_MAIN

  // Toggle Touchpad Navigation in SETTINGS_TOUCHPAD (row 2 after #R20 Animations)
  state = handleLauncherKey(state, 'DOWN'); // 1: Animations
  state = handleLauncherKey(state, 'DOWN'); // 2: Touchpad
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_TOUCHPAD');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.settings.touchpadMode, 'touchpad');
  assert.equal(state.settings.touchpadNav, false);
  state = handleLauncherKey(state, 'DOWN'); // Row 1: Glissement au pave (Mode 1)
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadNav, true);
  state = handleLauncherKey(state, 'ESC'); // back to SETTINGS_MAIN
  state = handleLauncherKey(state, 'ESC'); // back to LIBRARY

  // Test processTouchpadSample (sliding left/right/up/down, jitter filter, anti-burst, reversal)
  const tpr = createTouchpadTracker();
  assert.equal(processTouchpadSample(tpr, false, true, 500, 800, 2328, 1691), null);
  assert.equal(processTouchpadSample(tpr, false, true, 1600, 800, 2328, 1691), null);
  assert.equal(processTouchpadSample(tpr, true, true, 1000, 850, 2328, 1691), null);
  for (let j = 0; j < 8; j++) {
    assert.equal(
      processTouchpadSample(tpr, true, true, 1000 + (j % 2 ? 4 : -4), 850, 2328, 1691),
      null
    );
  }
  assert.equal(processTouchpadSample(tpr, true, true, 1180, 850, 2328, 1691), null);
  assert.equal(processTouchpadSample(tpr, true, true, 1360, 850, 2328, 1691), null);
  assert.equal(processTouchpadSample(tpr, true, true, 1540, 850, 2328, 1691), 'RIGHT');
  assert.equal(processTouchpadSample(tpr, true, true, 1750, 850, 2328, 1691), null);
  for (let cd = 0; cd < 4; cd++) {
    processTouchpadSample(tpr, true, true, 1750, 850, 2328, 1691);
  }
  assert.equal(processTouchpadSample(tpr, true, true, 1200, 850, 2328, 1691), 'LEFT');

  resetTouchpadTracker(tpr);
  assert.equal(processTouchpadSample(tpr, true, true, 1160, 1000, 2328, 1691), null);
  assert.equal(processTouchpadSample(tpr, true, true, 1160, 800, 2328, 1691), null);
  assert.equal(processTouchpadSample(tpr, true, true, 1160, 600, 2328, 1691), 'DOWN');
  for (let cd = 0; cd < 4; cd++) {
    processTouchpadSample(tpr, true, true, 1160, 600, 2328, 1691);
  }
  assert.equal(processTouchpadSample(tpr, true, true, 1160, 1000, 2328, 1691), 'UP');

  // Shortcut assign + reserved block + conflict replace + dynamic help + direct launch
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN'); // 1: Animations
  state = handleLauncherKey(state, 'DOWN'); // 2: Touchpad
  state = handleLauncherKey(state, 'DOWN'); // 3: Shortcuts
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_SHORTCUTS');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_SHORTCUTS');

  state = handleLauncherKey(state, 'EXE'); // pick key for game 0
  state = handleCharInput(state, '8'); // reserved
  assert.equal(state.toastMessage, 'TOUCHE SYSTEME RESERVEE');
  assert.equal(state.games[0].shortcutKey, '');

  state = handleCharInput(state, 'K');
  assert.equal(state.games[0].shortcutKey, 'K');

  // Assign K to game 1 -> conflict prompt -> replace
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'EXE');
  state = handleCharInput(state, 'K');
  assert.equal(state.sui.scConflictPrompt, true);
  state = handleLauncherKey(state, 'RIGHT'); // select Remplacer
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.scConflictPrompt, false);
  assert.equal(state.games[0].shortcutKey, '');
  assert.equal(state.games[1].shortcutKey, 'K');

  const defs = getShortcutDefs(state);
  assert.equal(defs.some((d) => d.keyLabel === 'TOUCHE K'), true);
  assert.equal(defs.some((d) => d.keyLabel === 'ENTER / 5'), true);
  assert.equal(defs.some((d) => d.keyLabel === 'TACTILE'), true);

  // Return to library & direct launch via K + exit with playtime delta
  state = handleLauncherKey(state, 'ESC');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'LIBRARY');
  const prevCount = state.games[1].launchCount;
  const prevPlay = state.games[1].playTimeSec;
  state = handleCharInput(state, 'k', 1794600000);
  assert.equal(state.runningGameIndex, 1);
  state = handleLauncherKey(state, 'ESC', 1794600000, 95);
  assert.equal(state.runningGameIndex, null);
  assert.equal(state.games[1].launchCount, prevCount + 1);
  assert.equal(state.games[1].playTimeSec, prevPlay + 95);
  assert.equal(formatDuration(95), '1m 35s');
  assert.equal(formatTimestamp(0), '--');

  // Verify all 7 sort orders run cleanly and preserve count
  for (const sOrder of SORT_ORDER_LIST) {
    state = handleLauncherKey(
      { ...state, settings: { ...state.settings, sortOrder: sOrder } },
      'LEFT'
    );
    assert.equal(state.filteredIndices.length, state.games.length);
  }

  // Test processTouchpadClick (center-click launch guard: works regardless of touchpadNav, blocks bounce, cancels on slide)
  const tpc = createTouchpadClickTracker();
  assert.equal(processTouchpadClick(tpc, true, true, true, 1000, 800, 2328, 1691), true);
  // Bounce/hold blocked immediately
  assert.equal(processTouchpadClick(tpc, true, true, true, 1000, 800, 2328, 1691), false);
  // Full release + cooldown re-arms click
  for (let rel = 0; rel < 6; rel++) {
    assert.equal(processTouchpadClick(tpc, false, false, false, 1000, 800, 2328, 1691), false);
  }
  // Finger slide > NL_TP_CLICK_SLIDE_CANCEL before pressing cancels launch
  assert.equal(processTouchpadClick(tpc, true, false, false, 1000, 800, 2328, 1691), false);
  const slideDelta = Math.ceil((NL_TP_CLICK_SLIDE_CANCEL + 40) * 2328 / 1024);
  assert.equal(processTouchpadClick(tpc, true, false, false, 1000 + slideDelta, 800, 2328, 1691), false);
  assert.equal(processTouchpadClick(tpc, true, true, true, 1000 + slideDelta, 800, 2328, 1691), false);
  // Release after canceled slide, then clean center click works again
  for (let rel = 0; rel < 3; rel++) {
    processTouchpadClick(tpc, false, false, false, 1000, 800, 2328, 1691);
  }
  assert.equal(processTouchpadClick(tpc, true, true, true, 1000, 800, 2328, 1691), true);

  // TACTILE launch shortcut remains documented whether touchpadNav is ON or OFF
  assert.equal(state.settings.touchpadNav, true);
  assert.equal(
    getShortcutDefs(state).some((d) => d.keyLabel === 'TACTILE' && /Lancer/i.test(d.actionLabel)),
    true
  );
  const stateTpOff = { ...state, settings: { ...state.settings, touchpadNav: false } };
  assert.equal(
    getShortcutDefs(stateTpOff).some((d) => d.keyLabel === 'TACTILE' && /Lancer/i.test(d.actionLabel)),
    true
  );

  // Direct inline library search in LIBRARY (no SETTINGS_SEARCH menu item)
  state = handleLauncherKey(state, 'TAB'); // activeCategory = 'FAVORITES'
  const prevCategory = state.activeCategory;
  state = handleCharInput(state, 'd');
  state = handleCharInput(state, 'o');
  state = handleCharInput(state, 'o');
  state = handleCharInput(state, 'm');
  assert.equal(state.sui.searchActive, true);
  assert.equal(state.sui.searchQuery, 'doom');
  assert.equal(state.activeCategory, prevCategory);

  // While searchActive is true, custom shortcut 'k' appends to query instead of launching directly
  state = handleCharInput(state, 'k');
  assert.equal(state.runningGameIndex, null);
  assert.equal(state.sui.searchQuery, 'doomk');
  state = handleLauncherKey(state, 'BACKSPACE');
  assert.equal(state.sui.searchQuery, 'doom');

  // SPACE appends a space while searching, and BACKSPACE deletes down to empty without closing the search bar
  state = handleLauncherKey(state, 'SPACE');
  assert.equal(state.sui.searchQuery, 'doom ');
  for (let b = 0; b < 5; b++) state = handleLauncherKey(state, 'BACKSPACE');
  assert.equal(state.sui.searchActive, true);
  assert.equal(state.sui.searchQuery, '');

  // Type 'doom' again in ALL category, launch with '5', then exit & close search with ESC preserving category
  state = handleLauncherKey(state, 'ESC'); // close empty search
  assert.equal(state.sui.searchActive, false);
  assert.equal(state.activeCategory, prevCategory);
  state = handleLauncherKey(state, 'ESC'); // FAV -> ALL
  assert.equal(state.activeCategory, 'ALL');

  state = handleCharInput(state, 'd');
  state = handleCharInput(state, 'o');
  state = handleCharInput(state, 'o');
  state = handleCharInput(state, 'm');
  assert.equal(state.sui.searchActive, true);
  assert.equal(state.filteredIndices.length >= 1, true);
  const doomIdx = state.filteredIndices[0];
  state = handleCharInput(state, '5', 1794600100);
  assert.equal(state.runningGameIndex, doomIdx);
  state = handleLauncherKey(state, 'ESC', 1794600100, 10);
  assert.equal(state.runningGameIndex, null);
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.searchActive, false);
  assert.equal(state.sui.searchQuery, '');

  // Safe Reset preserves favorites, covers, and stats while resetting launcher settings & shortcuts
  state = applyCapturedGameIcon(state, 0, 'data:image/png;base64,SAFE==', 'CAPTURED', false);
  const favGame0 = state.games[0].isFavorite;
  const launchesGame1 = state.games[1].launchCount;
  const playGame1 = state.games[1].playTimeSec;
  state = {
    ...state,
    displayMode: 'light',
    accentColor: 'red',
    settings: {
      autostart: true,
      globalHotkey: true,
      hotkeyCombo: 'shift_menu',
      layout: '4x4',
      showNames: true,
      showShortcuts: false,
      showBattery: false,
      touchpadMode: 'mouse',
      touchpadNav: true,
      touchpadSensitivity: 4,
      cursorSpeed: 4,
      touchpadCursorShow: true,
      touchpadCursorStyle: 'circle',
      cursorSize: 'large',
      tapToClick: true,
      sortOrder: 'play_time',
    },
  };
  state = resetSettingsOnly(state);
  assert.equal(state.displayMode, 'dark');
  assert.equal(state.accentColor, 'blue');
  assert.equal(state.settings.layout, '3x2');
  assert.equal(state.settings.showNames, false);
  assert.equal(state.settings.showShortcuts, true);
  assert.equal(state.settings.showBattery, false);
  assert.equal(state.settings.touchpadMode, 'touchpad');
  assert.equal(state.settings.touchpadNav, false);
  assert.equal(state.settings.touchpadSensitivity, NL_TP_SENS_DEFAULT);
  assert.equal(state.settings.cursorSpeed, NL_CURSOR_SPEED_DEFAULT);
  assert.equal(state.settings.touchpadCursorShow, false);
  assert.equal(state.settings.touchpadCursorStyle, 'arrow');
  assert.equal(state.settings.sortOrder, 'alpha_asc');
  assert.equal(state.settings.autostart, false);
  assert.equal(state.settings.globalHotkey, false);
  assert.equal(state.games[1].shortcutKey, '');
  // Preserved user/game data:
  assert.equal(state.games.length, 9);
  assert.equal(state.games[0].capturedIconDataUrl, 'data:image/png;base64,SAFE==');
  assert.equal(state.games[0].isFavorite, favGame0);
  assert.equal(state.games[1].launchCount, launchesGame1);
  assert.equal(state.games[1].playTimeSec, playGame1);
});

test('battery display removed cleanly & Settings -> Affichage exposes 5 streamlined options (#R15 scrollDir) + DEL poweroff', () => {
  assert.equal(NL_DISPLAY_OPT_COUNT, 5);

  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.settings.showBattery, false);
  assert.equal(state.settings.autostart, false);

  // Settings -> Affichage (row 0 of SETTINGS_MAIN) has 5 options (0..4), ending with poweroff at row 4
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_DISPLAY');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_DISPLAY');
  assert.equal(state.sui.displayCursor, 0);

  state = handleLauncherKey(state, 'DOWN'); // 1: Sens de defilement
  state = handleLauncherKey(state, 'DOWN'); // 2: Afficher les noms
  state = handleLauncherKey(state, 'DOWN'); // 3: Afficher les raccourcis
  state = handleLauncherKey(state, 'DOWN'); // 4: Eteindre la calculatrice
  assert.equal(state.sui.displayCursor, 4);
  assert.equal(state.sui.displayScroll, 2);

  // Wrap around from row 4 -> 0
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.displayCursor, 0);
  state = handleLauncherKey(state, 'UP');
  assert.equal(state.sui.displayCursor, 4);

  // Trigger power-off action on row 4
  assert.equal(state.sui.poweroffRequested, false);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.poweroffRequested, true);
  assert.equal(state.toastMessage, 'EXTINCTION CALCULATRICE...');

  // Verify DEL ('RESCAN') inside Settings also triggers power-off
  state = { ...state, sui: { ...state.sui, poweroffRequested: false } };
  state = handleLauncherKey(state, 'ESC'); // Back to SETTINGS_MAIN
  state = handleLauncherKey(state, 'RESCAN'); // DEL inside Settings
  assert.equal(state.sui.poweroffRequested, true);
  assert.equal(state.toastMessage, 'EXTINCTION CALCULATRICE...');

  // Verify DEL ('RESCAN') in LIBRARY strictly preserves library refresh ("BIBLIOTHEQUE ACTUALISEE")
  state = { ...state, sui: { ...state.sui, poweroffRequested: false } };
  state = handleLauncherKey(state, 'ESC'); // Back to LIBRARY
  assert.equal(state.sui.screen, 'LIBRARY');
  state = handleLauncherKey(state, 'RESCAN'); // DEL in Library
  assert.equal(state.sui.poweroffRequested, false);
  assert.equal(state.toastMessage, 'BIBLIOTHEQUE ACTUALISEE');

  // Returning from a game keeps showBattery false without extra optical VRAM probing
  state = handleLauncherKey(state, 'EXE', 1794606000);
  assert.notEqual(state.runningGameIndex, null);
  state = handleLauncherKey(state, 'ESC', 1794606120, 120);
  assert.equal(state.runningGameIndex, null);
  assert.equal(state.settings.showBattery, false);
});

test('settings menu visual & ergonomic overhaul: 16 icon-badged categories including SETTINGS_ANIMATIONS, SETTINGS_PROFILES and SETTINGS_DIAG without SETTINGS_AUTOSTART or SETTINGS_SEARCH', () => {
  assert.equal(SETTINGS_MENU_ITEMS.length, 16);
  assert.equal(
    SETTINGS_MENU_ITEMS.some((i) => (i.id as string) === 'SETTINGS_SEARCH'),
    false
  );
  assert.equal(
    SETTINGS_MENU_ITEMS.some((i) => (i.id as string) === 'SETTINGS_AUTOSTART'),
    false
  );

  const animItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_ANIMATIONS');
  const refreshItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_REFRESH_LIB');
  const hotkeyItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_HOTKEY');
  const profilesItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_PROFILES');
  const shortcutsItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_SHORTCUTS');
  const keyHelpItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_KEY_HELP');
  const capBatchItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_CAPTURE_BATCH');
  const iconsItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_ICONS');
  const diagItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_DIAG');
  assert.equal(animItem?.label, 'Animations');
  assert.equal(animItem?.icon, 'ANIM');
  assert.equal(refreshItem?.label, 'Rescanner les jeux');
  assert.equal(refreshItem?.icon, 'REFRESH');
  assert.equal(hotkeyItem?.label, 'Raccourci global TI-OS');
  assert.equal(hotkeyItem?.icon, 'HOTKEY');
  assert.equal(profilesItem?.label, 'Profils utilisateurs');
  assert.equal(profilesItem?.icon, 'PROFILES');
  assert.equal(shortcutsItem?.label, 'Touches de lancement');
  assert.equal(shortcutsItem?.icon, 'LAUNCH_KEY');
  assert.equal(keyHelpItem?.label, 'Commandes du launcher');
  assert.equal(keyHelpItem?.icon, 'KEYBOARD');
  assert.equal(capBatchItem?.label, "Generer captures d'ecran");
  assert.equal(capBatchItem?.icon, 'CAPTURE');
  assert.equal(iconsItem?.label, 'Gestion des icones');
  assert.equal(iconsItem?.icon, 'ICONS');
  assert.equal(diagItem?.label, 'Diagnostics & Journaux');
  assert.equal(diagItem?.icon, 'DIAG');

  const iconSet = new Set(SETTINGS_MENU_ITEMS.map((i) => i.icon));
  assert.equal(iconSet.size, 16);

  for (const item of SETTINGS_MENU_ITEMS) {
    assert.equal(item.label.length >= 6, true);
    assert.equal(item.subtitle.length >= 10, true);
  }

  assert.equal(SORT_ORDER_LABELS.alpha_asc, 'Ordre alphabetique (A -> Z)');
  assert.equal(SORT_ORDER_LABELS.alpha_desc, 'Ordre alphabetique (Z -> A)');
  assert.equal(SORT_ORDER_LABELS.recent_add, 'Ajoutes recemment');
  assert.equal(SORT_ORDER_LABELS.oldest_add, 'Ajoutes le plus tot');
  assert.equal(SORT_ORDER_LABELS.last_used, 'Joues recemment');
  assert.equal(SORT_ORDER_LABELS.most_launched, 'Les plus lances');
  assert.equal(SORT_ORDER_LABELS.play_time, 'Temps de jeu cumule');
});

test('3-file separation (.tns / .bmp.tns+.ico.tns / .scr.tns), batch capture queue, and Gestion des icones', () => {
  const zehnBuf = buildZehnTnsHeader({
    execName: 'nDoom',
    execAuthor: 'Mrakoplaz',
    version: 3,
    relocCount: 20,
    allocSize: 524288,
    totalSize: 145408,
    usesLcdBlit: true,
    supportsHww: true,
  });

  // 1. .scr.tns files are strictly excluded by the binary scanner
  const scrRes = inspectTnsBuffer(zehnBuf, 12800, '/documents/ndless/covers/nDoom.scr.tns');
  assert.equal(scrRes.isExecutable, false);
  assert.equal(scrRes.format, 'SYSTEM');

  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  const totalGames = state.games.length;
  assert.equal(totalGames, 9);

  // 2. Open Settings -> Generer captures d'ecran (index 7 after #R20 Animations)
  state = handleLauncherKey(state, 'SETTINGS');
  for (let i = 0; i < 7; i++) {
    state = handleLauncherKey(state, 'DOWN');
  }
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_CAPTURE_BATCH');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_CAPTURE_BATCH');
  assert.equal(state.sui.capActive, true);
  assert.equal(state.sui.capTotal, totalGames);
  assert.equal(state.sui.capPos, 0);

  // Skip game 0 via RIGHT ("Passer")
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.sui.capPos, 1);
  assert.equal(state.sui.capSkipCount, 1);

  // Simulate capturing game 1's real screenshot (.scr.tns) and assigning a custom .bmp.tns icon
  state = applyCapturedGameIcon(state, 1, 'data:image/png;base64,CUSTOM_BMP==', 'BMP', false);
  state = {
    ...state,
    games: state.games.map((g, idx) =>
      idx === 1
        ? { ...g, hasScreenshot: true, screenshotDataUrl: 'data:image/png;base64,REAL_SCR==' }
        : g
    ),
    sui: {
      ...state.sui,
      capDoneCount: 1,
      capPos: 2,
    },
  };

  // Cancel batch capture via ESC -> preserves completed captures
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.capActive, false);
  assert.equal(state.sui.capCancelled, true);
  assert.equal(state.games[1].hasScreenshot, true);
  assert.equal(state.games[1].screenshotDataUrl, 'data:image/png;base64,REAL_SCR==');

  // Return to SETTINGS_MAIN and open Gestion des icones (index 8)
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'SETTINGS_MAIN');
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_ICONS');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_ICONS');
  assert.equal(state.sui.iconCursor, 0);

  // Move right to game 1 (which has both custom BMP icon and real .scr.tns screenshot)
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.sui.iconCursor, 1);

  // Press EXE -> opens confirmation modal with safe default (iconConfirmYes = false)
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.iconConfirmModal, true);
  assert.equal(state.sui.iconConfirmYes, false);

  // Select "Supprimer" (RIGHT) and confirm (EXE)
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.sui.iconConfirmYes, true);
  state = handleLauncherKey(state, 'EXE');

  // Verify strict 3-file separation:
  // - Custom icon is deleted (reverts to NONE / null)
  // - Real screenshot (.scr.tns) is strictly preserved!
  // - Game executable (.tns) remains in library!
  assert.equal(state.sui.iconConfirmModal, false);
  assert.equal(state.games.length, totalGames);
  assert.equal(state.games[1].iconSource, 'NONE');
  assert.equal(state.games[1].capturedIconDataUrl, null);
  assert.equal(state.games[1].hasScreenshot, true);
  assert.equal(state.games[1].screenshotDataUrl, 'data:image/png;base64,REAL_SCR==');
});

test('genuine Windows .ico decoding (16-bit, 24-bit, 32-bit RGBA), corrupted/PNG rejection, automatic /documents/ndless/icons/<stem>.ico.tns association, fallback, and strict separation from .scr.tns', () => {
  assert.equal(extractGameStem('nDoom.tns'), 'nDoom');
  assert.equal(extractGameStem('/documents/Games/2048-CX.tns'), '2048-CX');

  // 1. Decode genuine 32-bit RGBA Windows .ico (94x68) with PNG output & preserved dimensions
  const ico32 = buildWindowsIcoBuffer({
    width: 94,
    height: 68,
    bpp: 32,
    topColorRgb: [220, 38, 38],
    bottomColorRgb: [30, 58, 138],
    borderColorRgb: [250, 204, 21],
    alpha: 255,
  });
  assert.equal(ico32[0], 0x00);
  assert.equal(ico32[1], 0x00);
  assert.equal(ico32[2], 0x01);
  assert.equal(ico32[3], 0x00);
  const dec32 = decodeWindowsIcoBuffer(ico32);
  assert.notEqual(dec32, null);
  assert.equal(dec32!.width, 84);
  assert.equal(dec32!.height, 61);
  assert.equal(dec32!.bpp, 32);
  assert.equal(dec32!.rgb565Pixels.length, 84 * 61);
  assert.match(dec32!.dataUrl, /^data:image\/png;base64,/);

  // 2. Decode genuine 24-bit RGB Windows .ico (64x64 square -> zero crop, zero upscaling, preserved 64x64)
  const ico24 = buildWindowsIcoBuffer({
    width: 64,
    height: 64,
    bpp: 24,
    topColorRgb: [16, 185, 129],
    bottomColorRgb: [6, 78, 59],
  });
  const dec24 = decodeWindowsIcoBuffer(ico24);
  assert.notEqual(dec24, null);
  assert.equal(dec24!.width, 64);
  assert.equal(dec24!.height, 64);
  assert.equal(dec24!.bpp, 24);
  assert.equal(dec24!.rgb565Pixels.length, 64 * 64);

  // 2b. Aspect-preserving downscale of non-square high-res 16:9 (160x90 -> 84x47) & 3:4 (120x160 -> 63x84)
  const ico16x9 = buildWindowsIcoBuffer({ width: 160, height: 90, bpp: 32, alpha: 255 });
  const dec16x9 = decodeWindowsIcoBuffer(ico16x9);
  assert.notEqual(dec16x9, null);
  assert.equal(dec16x9!.width, 84);
  assert.equal(dec16x9!.height, 47);

  const ico3x4 = buildWindowsIcoBuffer({ width: 120, height: 160, bpp: 32, alpha: 255 });
  const dec3x4 = decodeWindowsIcoBuffer(ico3x4);
  assert.notEqual(dec3x4, null);
  assert.equal(dec3x4!.width, 63);
  assert.equal(dec3x4!.height, 84);

  // 3. Decode genuine 16-bit XRGB1555 Windows .ico (94x68 -> aspect-preserving 84x61)
  const ico16 = buildWindowsIcoBuffer({
    width: 94,
    height: 68,
    bpp: 16,
    topColorRgb: [245, 158, 11],
    bottomColorRgb: [120, 53, 15],
  });
  const dec16 = decodeWindowsIcoBuffer(ico16);
  assert.notEqual(dec16, null);
  assert.equal(dec16!.bpp, 16);
  assert.equal(dec16!.width, 84);
  assert.equal(dec16!.height, 61);
  assert.equal(dec16!.rgb565Pixels.length, 84 * 61);

  // 4. Decode 32-bit RGBA with alpha=0 (transparent) -> composites onto dark card background RGB(25, 32, 44) in RGB565 & outputs PNG
  const icoTransparent = buildWindowsIcoBuffer({
    width: 32,
    height: 32,
    bpp: 32,
    alpha: 0,
  });
  const decTrans = decodeWindowsIcoBuffer(icoTransparent);
  assert.notEqual(decTrans, null);
  assert.equal(decTrans!.width, 32);
  assert.equal(decTrans!.height, 32);
  const expectedDarkBg565 = ((25 & 0xf8) << 8) | ((32 & 0xfc) << 3) | (44 >>> 3);
  assert.equal(decTrans!.rgb565Pixels[0], expectedDarkBg565);

  // 5. Reject corrupted, truncated, or PNG-compressed .ico files safely without crashing
  assert.equal(decodeWindowsIcoBuffer(new Uint8Array(0)), null);
  assert.equal(decodeWindowsIcoBuffer(new Uint8Array([0, 0, 1, 0, 1])), null); // truncated header
  const badMagic = new Uint8Array(ico32);
  badMagic[2] = 2; // CUR instead of ICO
  assert.equal(decodeWindowsIcoBuffer(badMagic), null);
  const pngPayload = new Uint8Array(ico32);
  pngPayload[22] = 0x89;
  pngPayload[23] = 0x50;
  pngPayload[24] = 0x4e;
  pngPayload[25] = 0x47;
  assert.equal(decodeWindowsIcoBuffer(pngPayload), null);

  // 6. Automatic association in scanVirtualFilesystem from /documents/ndless/icons/<stem>.ico.tns
  //    + fallback to NONE for games without an icon or with an invalid/corrupted .ico.tns
  const baseFiles = createInitialVirtualFilesystem();
  const sampleIcos = createSampleCustomIcoFiles(); // nDoom.ico.tns, gbc4nspire.ico.tns, 2048-CX.ico.tns
  const corruptedCraftiIco = {
    path: '/documents/ndless/icons/Crafti.ico.tns',
    sizeBytes: 32,
    headerBytes: new Uint8Array([0, 0, 1, 0, 1, 0, 94, 68, 0, 0, 1, 0, 32, 0, 200, 0, 0, 0, 22, 0, 0, 0]),
  };
  let state = scanVirtualFilesystem([...baseFiles, ...sampleIcos, corruptedCraftiIco]);
  assert.equal(state.games.length, 9);

  const gNDoom = state.games.find((g) => g.filename === 'nDoom.tns')!;
  const gGbc = state.games.find((g) => g.filename === 'gbc4nspire.tns')!;
  const g2048 = state.games.find((g) => g.filename === '2048-CX.tns')!;
  const gCrafti = state.games.find((g) => g.filename === 'Crafti.tns')!;
  const gQuake = state.games.find((g) => g.filename === 'nQuake.tns')!;

  assert.equal(gNDoom.iconSource, 'ICO');
  assert.notEqual(gNDoom.capturedIconDataUrl, null);
  assert.equal(gGbc.iconSource, 'ICO');
  assert.notEqual(gGbc.capturedIconDataUrl, null);
  assert.equal(g2048.iconSource, 'ICO');
  assert.notEqual(g2048.capturedIconDataUrl, null);

  // Corrupted .ico.tns or absent .ico.tns cleanly falls back to default emblem ('NONE')
  assert.equal(gCrafti.iconSource, 'NONE');
  assert.equal(gCrafti.capturedIconDataUrl, null);
  assert.equal(gQuake.iconSource, 'NONE');
  assert.equal(gQuake.capturedIconDataUrl, null);

  // 7. Strict separation: capturing a gameplay screenshot (.scr.tns) never overwrites the custom .ico icon
  //    or replaces a default emblem ('NONE') in the library
  const nDoomIdx = state.games.findIndex((g) => g.filename === 'nDoom.tns');
  const quakeIdx = state.games.findIndex((g) => g.filename === 'nQuake.tns');
  const nDoomOriginalIcoUrl = state.games[nDoomIdx].capturedIconDataUrl;

  state = applyCapturedScreenshot(state, nDoomIdx, 'data:image/png;base64,NDOOM_SCR==', true);
  state = applyCapturedScreenshot(state, quakeIdx, 'data:image/png;base64,QUAKE_SCR==', true);

  assert.equal(state.games[nDoomIdx].iconSource, 'ICO');
  assert.equal(state.games[nDoomIdx].capturedIconDataUrl, nDoomOriginalIcoUrl);
  assert.equal(state.games[nDoomIdx].hasScreenshot, true);
  assert.equal(state.games[nDoomIdx].screenshotDataUrl, 'data:image/png;base64,NDOOM_SCR==');

  assert.equal(state.games[quakeIdx].iconSource, 'NONE');
  assert.equal(state.games[quakeIdx].capturedIconDataUrl, null);
  assert.equal(state.games[quakeIdx].hasScreenshot, true);
  assert.equal(state.games[quakeIdx].screenshotDataUrl, 'data:image/png;base64,QUAKE_SCR==');

  // 8. Navigation across games, pages, and categories preserves all custom .ico icons
  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'PAGE_NEXT');
  state = handleLauncherKey(state, 'TAB'); // FAV
  state = handleLauncherKey(state, 'TAB'); // RECENT
  state = handleLauncherKey(state, 'TAB'); // ALL
  assert.equal(state.games[nDoomIdx].iconSource, 'ICO');
  assert.equal(state.games[nDoomIdx].capturedIconDataUrl, nDoomOriginalIcoUrl);

  // 9. Indexed 8bpp / 4bpp / 1bpp palette ICO + 1-bit AND mask + PNG-compressed ICO + symmetric transparent trim + .ico fallback
  const buildIndexedIco = (ibpp: 1 | 4 | 8, iw: number, ih: number) => {
    const palCount = 1 << ibpp;
    const xorStride = (((iw * ibpp + 31) >>> 5) * 4);
    const andStride = (((iw + 31) >>> 5) * 4);
    const dibSize = 40 + palCount * 4 + xorStride * ih + andStride * ih;
    const buf = new Uint8Array(22 + dibSize);
    buf[2] = 1; buf[4] = 1;
    buf[6] = iw; buf[7] = ih; buf[8] = palCount & 0xff; buf[10] = 1; buf[12] = ibpp;
    buf[14] = dibSize & 0xff; buf[15] = (dibSize >> 8) & 0xff; buf[18] = 22;
    buf[22] = 40; buf[26] = iw; buf[30] = ih * 2; buf[34] = 1; buf[36] = ibpp;
    const palOff = 62;
    // entry 0: pure green, entry 1: pure orange-red
    buf[palOff + 0] = 20; buf[palOff + 1] = 220; buf[palOff + 2] = 40;
    buf[palOff + 4] = 10; buf[palOff + 5] = 80; buf[palOff + 6] = 240;
    const xorOff = palOff + palCount * 4;
    const andOff = xorOff + xorStride * ih;
    for (let y = 0; y < ih; y++) {
      const fRow = ih - 1 - y;
      for (let x = 0; x < iw; x++) {
        const idx = x >= iw / 2 ? 1 : 0;
        if (ibpp === 8) buf[xorOff + fRow * xorStride + x] = idx;
        else if (ibpp === 4) {
          const byteOff = xorOff + fRow * xorStride + (x >> 1);
          buf[byteOff] |= idx << ((x & 1) ? 0 : 4);
        } else {
          const byteOff = xorOff + fRow * xorStride + (x >> 3);
          buf[byteOff] |= idx << (7 - (x & 7));
        }
        if (y === 0) {
          // Top row masked transparent via AND mask bit = 1
          buf[andOff + fRow * andStride + (x >> 3)] |= 1 << (7 - (x & 7));
        }
      }
    }
    return buf;
  };

  for (const ibpp of [8, 4, 1] as const) {
    const dec = decodeWindowsIcoBuffer(buildIndexedIco(ibpp, 20, 20));
    assert.notEqual(dec, null);
    assert.equal(dec!.width, 20);
    assert.equal(dec!.height, 20);
    // Top row masked transparent -> composites to card bg RGB(25, 32, 44)
    assert.equal(dec!.rgb565Pixels[0], expectedDarkBg565);
    // Bottom-left pixel opaque green (R=40, G=220, B=20), bottom-right orange-red (R=240, G=80, B=10)
    const expGreen = ((40 & 0xf8) << 8) | ((220 & 0xfc) << 3) | (20 >>> 3);
    const expOrange = ((240 & 0xf8) << 8) | ((80 & 0xfc) << 3) | (10 >>> 3);
    assert.equal(dec!.rgb565Pixels[15 * 20 + 2], expGreen);
    assert.equal(dec!.rgb565Pixels[15 * 20 + 18], expOrange);
  }

  // PNG-compressed .ico + symmetric transparent outer margin trim (48x48 with 8px transparent border -> trimmed tightly around 32x32 art)
  const paddedRgba = new Uint8Array(48 * 48 * 4);
  for (let y = 8; y < 40; y++) {
    for (let x = 8; x < 40; x++) {
      const o = (y * 48 + x) * 4;
      paddedRgba[o] = 56;
      paddedRgba[o + 1] = 189;
      paddedRgba[o + 2] = 248;
      paddedRgba[o + 3] = 255;
    }
  }
  const pngIco = buildPngIcoBuffer(paddedRgba, 48, 48);
  const decPng = decodeWindowsIcoBuffer(pngIco);
  assert.notEqual(decPng, null);
  assert.equal(decPng!.sourceWidth, 34);
  assert.equal(decPng!.sourceHeight, 34);
  assert.equal(decPng!.width, 34);
  assert.equal(decPng!.height, 34);

  // Default simulator filesystem gives every bundled game a real .ico.tns custom icon
  const simFiles = createDefaultSimulatorFilesystem();
  const simState = scanVirtualFilesystem(simFiles);
  assert.equal(simState.games.length, 9);
  assert.equal(simState.games.every((g) => g.iconSource === 'ICO' && Boolean(g.capturedIconDataUrl)), true);

  // Direct `.ico` (without `.tns`) fallback in /documents/ndless/icons/
  const rawIcoState = scanVirtualFilesystem([
    ...baseFiles,
    {
      path: '/documents/ndless/icons/nQuake.ico',
      sizeBytes: pngIco.length,
      headerBytes: pngIco,
    },
  ]);
  const rawQuake = rawIcoState.games.find((g) => g.filename === 'nQuake.tns')!;
  assert.equal(rawQuake.iconSource, 'ICO');
  assert.notEqual(rawQuake.capturedIconDataUrl, null);
});

test('touchpad 5-detent sensitivity slider: thresholds, keypad/pointer control, swipe sensitivity & reset default', () => {
  assert.equal(NL_TP_SENS_COUNT, 5);
  assert.equal(NL_TP_SENS_DEFAULT, 2);
  assert.deepEqual(NL_TP_THRESHOLDS, [300, 255, 215, 175, 138]);
  assert.equal(TP_SENS_LABELS.length, 5);

  assert.equal(clampTouchpadSensitivity(-5), 0);
  assert.equal(clampTouchpadSensitivity(0), 0);
  assert.equal(clampTouchpadSensitivity(2.6), 3);
  assert.equal(clampTouchpadSensitivity(99), 4);
  assert.equal(clampTouchpadSensitivity(NaN), NL_TP_SENS_DEFAULT);

  for (let lvl = 0; lvl < NL_TP_SENS_COUNT; lvl++) {
    assert.equal(getTouchpadThresholdForLevel(lvl), NL_TP_THRESHOLDS[lvl]);
  }

  // Verify swipe distance sensitivity: 390 raw delta triggers immediately at fast (level 4, th=138),
  // but is still below slow threshold (level 0, th=300)
  const tprFast = createTouchpadTracker();
  assert.equal(processTouchpadSample(tprFast, true, true, 1000, 850, 2328, 1691, false, 4), null);
  assert.equal(processTouchpadSample(tprFast, true, true, 1390, 850, 2328, 1691, false, 4), 'RIGHT');

  const tprSlow = createTouchpadTracker();
  assert.equal(processTouchpadSample(tprSlow, true, true, 1000, 850, 2328, 1691, false, 0), null);
  assert.equal(processTouchpadSample(tprSlow, true, true, 1390, 850, 2328, 1691, false, 0), null);
  // Larger slide finally crosses slow threshold (300)
  assert.equal(processTouchpadSample(tprSlow, true, true, 1800, 850, 2328, 1691, false, 0), 'RIGHT');

  // SETTINGS_TOUCHPAD keypad navigation (row 0 = Mode, row 1 = Glissement, row 2 = 5-detent sensitivity slider)
  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.settings.touchpadSensitivity, 2);
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN'); // 1: Animations
  state = handleLauncherKey(state, 'DOWN'); // 2: Touchpad
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_TOUCHPAD');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.sui.tpCursor, 0);

  // Move to sensitivity slider row (row 2) and step across detents 2 -> 3 -> 4 (clamped) -> 3 -> 0 (clamped)
  state = handleLauncherKey(state, 'DOWN'); // Row 1: Glissement
  assert.equal(state.sui.tpCursor, 1);
  state = handleLauncherKey(state, 'DOWN'); // Row 2: Sensibilite pave
  assert.equal(state.sui.tpCursor, 2);
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.touchpadSensitivity, 3);
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.touchpadSensitivity, 4);
  state = handleLauncherKey(state, 'RIGHT'); // upper clamp
  assert.equal(state.settings.touchpadSensitivity, 4);

  for (let i = 0; i < 6; i++) {
    state = handleLauncherKey(state, 'LEFT');
  }
  assert.equal(state.settings.touchpadSensitivity, 0); // lower clamp

  // Direct canvas pointer/detent control via setTouchpadControl
  state = setTouchpadControl(state, 2, 3, false);
  assert.equal(state.sui.tpCursor, 2);
  assert.equal(state.settings.touchpadSensitivity, 3);
  state = setTouchpadControl(state, 1, undefined, true);
  assert.equal(state.sui.tpCursor, 1);
  assert.equal(state.settings.touchpadNav, true);

  // Saved in scanVirtualFilesystem and restored by resetSettingsOnly
  const reloaded = scanVirtualFilesystem(
    createInitialVirtualFilesystem(),
    '/documents/ndless/N-Launcher.tns',
    [],
    2,
    { settings: state.settings }
  );
  assert.equal(reloaded.settings.touchpadSensitivity, 3);
  const reset = resetSettingsOnly(reloaded);
  assert.equal(reset.settings.touchpadSensitivity, NL_TP_SENS_DEFAULT);
});

test('autostart & battery completely removed in #R7: forced disabled across legacy prefs, persistence toggles & reset while preserving user data', () => {
  let state = scanVirtualFilesystem(
    createDefaultSimulatorFilesystem(),
    '/documents/ndless/N-Launcher.tns',
    [],
    42,
    {
      displayMode: 'gray',
      accentColor: 'red',
      settings: {
        autostart: true,
        showBattery: true,
        layout: '4x4',
        showNames: true,
        sortOrder: 'play_time',
      },
    }
  );

  // Even when legacy saved prefs pass autostart=true and showBattery=true, #R7 forces both OFF
  assert.equal(state.settings.autostart, false);
  assert.equal(state.sys.autostartActive, false);
  assert.equal(state.settings.showBattery, false);

  // Customize favorite, shortcut key, and play stats on first game
  state = {
    ...state,
    games: state.games.map((g, idx) =>
      idx === 0
        ? {
            ...g,
            isFavorite: true,
            launchCount: 17,
            playTimeSec: 3600,
            lastPlayedTs: 1762900325,
            shortcutKey: 'C',
          }
        : g
    ),
  };

  // Switching simulated Ndless persistence never enables autostart
  state = toggleSimulatedNdlessPersistence(state);
  assert.equal(state.settings.autostart, false);
  assert.equal(state.sys.autostartActive, false);
  state = toggleSimulatedNdlessPersistence(state);
  assert.equal(state.settings.autostart, false);
  assert.equal(state.sys.autostartActive, false);

  // Any legacy navigation into SETTINGS_AUTOSTART returns safely to SETTINGS_MAIN without enabling autostart
  const redirected = handleLauncherKey(
    {
      ...state,
      sui: { ...state.sui, screen: 'SETTINGS_AUTOSTART' },
    },
    'EXE'
  );
  assert.equal(redirected.sui.screen, 'SETTINGS_MAIN');
  assert.equal(redirected.settings.autostart, false);
  assert.equal(redirected.sys.autostartActive, false);

  // User theme, layout, sort, favorites, play stats, shortcuts & playCounter remain 100% intact across rescan & reset
  const rescanned = scanVirtualFilesystem(
    createDefaultSimulatorFilesystem(),
    '/documents/ndless/N-Launcher.tns',
    state.games,
    state.playCounter,
    {
      displayMode: state.displayMode,
      accentColor: state.accentColor,
      settings: { ...state.settings, autostart: true, showBattery: true },
    }
  );
  assert.equal(rescanned.settings.autostart, false);
  assert.equal(rescanned.settings.showBattery, false);
  assert.equal(rescanned.displayMode, 'gray');
  assert.equal(rescanned.accentColor, 'red');
  assert.equal(rescanned.settings.layout, '4x4');
  assert.equal(rescanned.settings.showNames, true);
  assert.equal(rescanned.settings.sortOrder, 'play_time');
  assert.equal(rescanned.playCounter, 42);
  assert.equal(rescanned.games[0].isFavorite, true);
  assert.equal(rescanned.games[0].launchCount, 17);
  assert.equal(rescanned.games[0].playTimeSec, 3600);
  assert.equal(rescanned.games[0].lastPlayedTs, 1762900325);
  assert.equal(rescanned.games[0].shortcutKey, 'C');

  const afterReset = resetSettingsOnly(rescanned);
  assert.equal(afterReset.settings.autostart, false);
  assert.equal(afterReset.settings.showBattery, false);
  assert.equal(afterReset.games[0].isFavorite, true);
  assert.equal(afterReset.games[0].launchCount, 17);
});

test('persistent library cache CRC32, Actualiser la bibliotheque in Settings, and global TI-OS hotkey (Ctrl + Menu)', () => {
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);
  assert.equal(state.sys.cacheValid, true);
  assert.notEqual(state.sys.cacheCrc32, 0);
  const initialCrc = computeLibraryCacheCrc32(state.games);
  assert.equal(state.sys.cacheCrc32, initialCrc);

  // Modifying icon or game entry changes CRC32 deterministically
  const modifiedState = applyCapturedGameIcon(state, 0, 'data:image/png;base64,NEW_ICO==', 'ICO', false);
  const nextCrc = computeLibraryCacheCrc32(modifiedState.games);
  assert.notEqual(nextCrc, initialCrc);

  // Default globalHotkey is strictly OFF (false)
  assert.equal(state.settings.globalHotkey, false);
  assert.equal(state.sys.hotkeyActive, false);

  // Open Settings -> item 9 is Actualiser la bibliotheque (SETTINGS_REFRESH_LIB)
  state = { ...state, sys: { ...state.sys, cacheValid: false } };
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(state.sui.screen, 'SETTINGS_MAIN');
  for (let i = 0; i < 9; i++) {
    state = handleLauncherKey(state, 'DOWN');
  }
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_REFRESH_LIB');
  state = handleLauncherKey(state, 'EXE');
  // Executing SETTINGS_REFRESH_LIB opens dedicated SETTINGS_RESCAN summary screen & rebuilds CRC32 cache
  assert.equal(state.sui.screen, 'SETTINGS_RESCAN');
  assert.equal(state.sui.rescan.finished, true);
  assert.equal(state.sui.rescan.gamesFound, state.games.length);
  assert.equal(state.sys.cacheValid, true);
  assert.equal(state.toastMessage, `RESCAN OK : ${state.games.length} JEU(X)`);

  // Close SETTINGS_RESCAN back to SETTINGS_MAIN, then open item 9 (Raccourci global TI-OS)
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_MAIN');
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_HOTKEY');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_HOTKEY');

  // Enable and disable globalHotkey in SETTINGS_HOTKEY
  assert.equal(state.sui.hotkeyCursor, 0);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.globalHotkey, true);
  assert.equal(state.sys.hotkeyActive, true);
  assert.equal(state.sys.hotkeyRamActive, true);
  assert.equal(state.toastMessage, 'Raccourci (Ctrl + Menu): ON');
  assert.equal(
    getShortcutDefs(state).some((d) => d.keyLabel === 'CTRL+MENU' && d.actionLabel.includes('(ON)')),
    true
  );

  // Move down to Row 1 (Combinaison) and cycle CTRL+MENU -> SHIFT+MENU -> CTRL+SHIFT+MENU
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.hotkeyCursor, 1);
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.hotkeyCombo, 'shift_menu');
  assert.equal(
    getShortcutDefs(state).some((d) => d.keyLabel === 'SHFT+MENU'),
    true
  );
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.hotkeyCombo, 'ctrl_shift_menu');
  assert.equal(
    getShortcutDefs(state).some((d) => d.keyLabel === 'C+S+MENU'),
    true
  );

  // Mouse interaction on SETTINGS_HOTKEY: Row 1 click cycles combo back to ctrl_menu, Row 0 click disables
  state = {
    ...state,
    settings: { ...state.settings, touchpadMode: 'mouse', touchpadCursorShow: true },
  };
  state = moveMouseTo(state, 160, 96);
  assert.equal(state.sui.hoverKind, 'SET_ROW');
  assert.equal(state.sui.hoverIndex, 1);
  const row1Click = clickMouse(state);
  assert.equal(row1Click.handled, true);
  state = row1Click.state;
  assert.equal(state.settings.hotkeyCombo, 'ctrl_menu');

  state = moveMouseTo(state, 160, 50);
  assert.equal(state.sui.hoverKind, 'SET_ROW');
  assert.equal(state.sui.hoverIndex, 0);
  const row0Click = clickMouse(state);
  assert.equal(row0Click.handled, true);
  state = row0Click.state;
  assert.equal(state.settings.globalHotkey, false);
  assert.equal(state.sys.hotkeyActive, false);
  assert.equal(state.sys.hotkeyRamActive, false);
  assert.equal(state.toastMessage, 'Raccourci TI-OS : OFF');

  // Re-enable and verify resetSettingsOnly cleanly disables globalHotkey and unhooks nl_hotkey.tns
  assert.equal(state.sui.hotkeyCursor, 0);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.globalHotkey, true);
  assert.equal(state.sys.hotkeyRamActive, true);
  const resetState = resetSettingsOnly(state);
  assert.equal(resetState.settings.autostart, false);
  assert.equal(resetState.settings.globalHotkey, false);
  assert.equal(resetState.settings.hotkeyCombo, 'ctrl_menu');
  assert.equal(resetState.sys.hotkeyActive, false);
  assert.equal(resetState.sys.hotkeyRamActive, false);
});

test('last_used sort (« Joues recemment ») keeps library order frozen during launch and reorders only upon returning from the game', () => {
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);

  // Switch sort order to 'last_used' via Settings -> Tri de la bibliotheque
  state = handleLauncherKey(
    {
      ...state,
      settings: { ...state.settings, sortOrder: 'last_used' },
    },
    'RESCAN'
  );
  assert.equal(state.settings.sortOrder, 'last_used');
  const orderBeforeLaunch = [...state.filteredIndices];
  assert.equal(orderBeforeLaunch.length, 9);

  // Navigate to slot 3 (4th game in the grid)
  state = handleLauncherKey(state, 'RIGHT'); // 1
  state = handleLauncherKey(state, 'RIGHT'); // 2
  state = handleLauncherKey(state, 'RIGHT'); // 3
  assert.equal(state.selectedPos, 3);
  const launchedGameIdx = state.filteredIndices[3];
  assert.notEqual(launchedGameIdx, orderBeforeLaunch[0]);

  // 1. Press EXE to launch the game at slot 3:
  //    - The launched game MUST keep its current position (slot 3) during the launch transition
  //    - Zero items in filteredIndices may move or reorder at launch time
  state = handleLauncherKey(state, 'EXE', 1795000100);
  assert.equal(state.runningGameIndex, launchedGameIdx);
  assert.equal(state.selectedPos, 3);
  assert.deepEqual(state.filteredIndices, orderBeforeLaunch);

  // 2. Exit the game via ESC and return to N-Launcher:
  //    - The library now updates the 'last_used' sort
  //    - The recently launched game moves to slot 0 (first position)
  state = handleLauncherKey(state, 'ESC', 1795000220, 120);
  assert.equal(state.runningGameIndex, null);
  assert.equal(state.filteredIndices[0], launchedGameIdx);
  assert.equal(state.selectedPos, 0);
  assert.equal(state.currentPage, 0);
  // The previous slot 0, 1, 2 games shifted cleanly to slots 1, 2, 3
  assert.equal(state.filteredIndices[1], orderBeforeLaunch[0]);
  assert.equal(state.filteredIndices[2], orderBeforeLaunch[1]);
  assert.equal(state.filteredIndices[3], orderBeforeLaunch[2]);
});

test('multi-profile system: 43 exclusive avatars in 4 categories, shortcuts ./0/-, isolation, Settings UI & safe deletion', () => {
  // 1. Verify all 43 official avatar assets & 4 categories (16 + 9 + 9 + 9 = 43)
  assert.equal(NL_AVATAR_COUNT, 43);
  assert.equal(NL_MAX_PROFILES, 43);
  assert.equal(NL_AVATAR_CAT_COUNT, 4);
  assert.equal(NL_PROFILE_NAME_MAX, 15);
  assert.deepEqual(
    AVATAR_CATEGORIES.map((c) => c.count),
    [16, 9, 9, 9]
  );
  for (let id = 0; id < NL_AVATAR_COUNT; id++) {
    const url = getAvatarImageUrl(id);
    const diskPath = path.resolve(process.cwd(), 'public', url.replace(/^\//, ''));
    assert.equal(fs.existsSync(diskPath), true, `Missing avatar asset: ${diskPath}`);
    const cat = getAvatarCategoryOf(id);
    const { start, count } = getAvatarCategoryRange(cat);
    assert.equal(id >= start && id < start + count, true);
  }

  // 2. Verify '0' is freed from game shortcuts and '.', '0', '-' are reserved & documented
  assert.equal(ASSIGNABLE_SHORTCUT_KEYS.includes('0'), false);
  assert.equal(isShortcutReserved('.'), true);
  assert.equal(isShortcutReserved('0'), true);
  assert.equal(isShortcutReserved('-'), true);
  assert.equal(isShortcutAssignable('0'), false);

  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);
  const defs = getShortcutDefs(state);
  assert.equal(defs.some((d) => d.keyLabel === '.'), true);
  assert.equal(defs.some((d) => d.keyLabel === '0 / -'), true);

  // Primary profile exists by default with avatar 0
  assert.equal(state.profiles.items.length, 1);
  assert.equal(state.profiles.activeIndex, 0);
  assert.equal(getActiveProfile(state).isPrimary, true);
  assert.equal(getActiveProfile(state).name, 'Profil 1');
  assert.equal(findAvatarOwner(state, 0, -1), 0);

  // 3. Per-profile isolation test: modify P1 favorites, stats, layout, sortOrder, shortcut
  state = handleLauncherKey(state, 'FAV'); // game 0 favorite in P1
  const game0Title = state.games[state.filteredIndices[0]].title;
  state = handleLauncherKey(state, 'EXE', 1796000100);
  state = handleLauncherKey(state, 'ESC', 1796000200, 100);
  state = {
    ...state,
    settings: {
      ...state.settings,
      layout: '4x4',
      showNames: true,
      sortOrder: 'last_used',
    },
    games: state.games.map((g, i) => (i === 0 ? { ...g, shortcutKey: 'A' } : g)),
  };

  // Create Profile 2 with avatar 5 -> exclusive check prevents using avatar 0
  const takenAttempt = createUserProfile(state, 'Bob', 0);
  assert.equal(takenAttempt.createdIdx, -1);

  const p2Res = createUserProfile(state, 'Alice', 5);
  assert.equal(p2Res.createdIdx, 1);
  state = p2Res.state;
  assert.equal(state.profiles.items.length, 2);

  // Switch to Alice (index 1) -> global preferences (displayMode/accentColor/games) shared,
  // but Alice's personal favorites, stats, layout, sortOrder, and shortcuts start fresh!
  state = switchProfileTo(state, 1);
  assert.equal(state.profiles.activeIndex, 1);
  assert.equal(getActiveProfile(state).name, 'Alice');
  assert.equal(
    state.games.some((g) => g.isFavorite),
    false
  );
  assert.equal(
    state.games.some((g) => g.launchCount > 0),
    false
  );
  assert.equal(
    state.games.some((g) => Boolean(g.shortcutKey)),
    false
  );
  assert.equal(state.settings.layout, '3x2');
  assert.equal(state.settings.showNames, false);
  assert.equal(state.settings.sortOrder, 'alpha_asc');

  // Switch back via '0' (PROFILE_PREV, no wrap) and verify '-' (PROFILE_NEXT, no wrap)
  state = handleLauncherKey(state, 'PROFILE_NEXT'); // Already at last (index 1) -> stays 1
  assert.equal(state.profiles.activeIndex, 1);
  state = handleLauncherKey(state, 'PROFILE_PREV'); // Moves to index 0 (Profil 1)
  assert.equal(state.profiles.activeIndex, 0);
  assert.equal(state.settings.layout, '4x4');
  assert.equal(state.settings.showNames, true);
  assert.equal(state.games.some((g) => g.title === game0Title && g.isFavorite), true);
  assert.equal(state.games[0].shortcutKey, 'A');
  state = handleLauncherKey(state, 'PROFILE_PREV'); // Already at index 0 -> no wrap!
  assert.equal(state.profiles.activeIndex, 0);

  // 4. Typing '0' or '-' in search does NOT switch profiles
  state = handleCharInput(state, 'N');
  state = handleCharInput(state, '0');
  assert.equal(state.sui.searchActive, true);
  assert.equal(state.sui.searchQuery, 'N0');
  assert.equal(state.profiles.activeIndex, 0);
  state = handleLauncherKey(state, 'ESC'); // close search

  // 5. Quick Profile Selector ('.') open, browse, '+' flow into direct avatar selection (no name prompt) & return to Quick Selector
  state = handleLauncherKey(state, 'PROFILE_QUICK');
  assert.equal(state.sui.qpOpen, true);
  assert.equal(state.sui.qpCursor, 0);
  state = handleLauncherKey(state, 'RIGHT'); // index 1 (Alice)
  const prevSwitchSeq = state.profSwitchSeq ?? 0;
  state = handleLauncherKey(state, 'EXE'); // Switches to Alice with 4-stage water-drop + ghost zoom transition
  assert.equal(state.profiles.activeIndex, 1);
  assert.equal(state.profSwitchSeq, prevSwitchSeq + 1);
  assert.equal(state.profSwitchAvatarId, getActiveProfile(state).avatarId);
  assert.equal(getActiveProfile(state).name, 'Alice');

  // Reopen Quick Selector, navigate to '+' slot, trigger direct avatar creation
  state = handleLauncherKey(state, 'PROFILE_QUICK');
  assert.equal(state.sui.qpOpen, true);
  state = handleLauncherKey(state, 'RIGHT'); // index 2 ('+' create slot)
  assert.equal(state.sui.qpCursor, 2);
  const prevPlusSeq = state.profPlusSeq ?? 0;
  state = handleLauncherKey(state, 'EXE'); // Opens SETTINGS_PROFILES -> CREATE_AVATAR directly (auto-named "Profil 3")
  assert.equal(state.sui.qpOpen, false);
  assert.equal(state.sui.screen, 'SETTINGS_PROFILES');
  assert.equal(state.sui.profMode, 'CREATE_AVATAR');
  assert.equal(state.sui.profFromQuick, true);
  assert.equal(state.sui.profEditName, '');
  assert.equal(state.profPlusSeq, prevPlusSeq + 1);
  state = handleLauncherKey(state, 'TAB'); // Next category
  assert.equal(state.sui.profAvatarCat, 1);
  const prevBirthSeq = state.profBirthSeq ?? 0;
  state = handleLauncherKey(state, 'EXE'); // Confirms creation, triggers birth animation & returns to Quick Selector!
  assert.equal(state.profiles.items.length, 3);
  assert.equal(getActiveProfile(state).name, 'Profil 3');
  assert.equal(state.sui.screen, 'LIBRARY');
  assert.equal(state.sui.profMode, 'LIST');
  assert.equal(state.sui.qpOpen, true);
  assert.equal(state.sui.qpCursor, 2);
  assert.equal(state.profBirthSeq, prevBirthSeq + 1);
  assert.equal(state.profBirthAvatarId, getActiveProfile(state).avatarId);
  state = handleLauncherKey(state, 'ESC'); // Close Quick Selector

  // 6. Avatar replacement frees previous avatar & deleting non-primary profile keeps games intact
  const oldCh0Avatar = getActiveProfile(state).avatarId;
  const avChange = setUserProfileAvatar(state, 2, 10);
  assert.equal(avChange.ok, true);
  state = avChange.state;
  assert.equal(findAvatarOwner(state, oldCh0Avatar, -1), -1);
  assert.equal(findAvatarOwner(state, 10, -1), 2);
  const renRes = renameUserProfile(state, 2, 'Charlie');
  assert.equal(renRes.ok, true);
  assert.equal(renRes.state.profiles.items[2].name, 'Charlie');

  // Cannot delete Primary profile (index 0)
  const delPrimary = deleteUserProfile(renRes.state, 0);
  assert.equal(delPrimary.ok, false);

  // Deleting index 2 works cleanly, restores games list intact & returns avatar 10 to free pool
  const gamesBeforeDel = delPrimary.state.games.length;
  const delRes = deleteUserProfile(delPrimary.state, 2);
  assert.equal(delRes.ok, true);
  assert.equal(delRes.state.profiles.items.length, 2);
  assert.equal(delRes.state.games.length, gamesBeforeDel);
  assert.equal(findAvatarOwner(delRes.state, 10, -1), -1);

  // 7. Full pool cap at 43 profiles + clean rejection on 44th
  let fullState = delRes.state;
  for (let n = fullState.profiles.items.length; n < NL_MAX_PROFILES; n++) {
    const freeId = firstFreeAvatar(fullState);
    assert.equal(freeId >= 0, true);
    fullState = createUserProfile(fullState, `User${n + 1}`, freeId).state;
  }
  assert.equal(fullState.profiles.items.length, 43);
  assert.equal(firstFreeAvatar(fullState), -1);
  const overflow = createUserProfile(fullState, 'Overflow', 0);
  assert.equal(overflow.createdIdx, -1);

  // 8. resetSettingsOnly keeps all profiles intact
  const afterReset = resetSettingsOnly(state);
  assert.equal(afterReset.profiles.items.length, 3);
});

test('adaptive 320x240 library screen occupancy, incomplete last row centering, and 4-side independent .ico margin trimming', () => {
  // 1. Adaptive 4x4 layout for 9, 10, 12, and 16 games (with & without names)
  const m9No = getLayoutMetrics('4x4', false, true, 9);
  const m9Nm = getLayoutMetrics('4x4', true, true, 9);
  const m10No = getLayoutMetrics('4x4', false, true, 10);
  const m12No = getLayoutMetrics('4x4', false, true, 12);
  const m16No = getLayoutMetrics('4x4', false, true, 16);
  const m16Nm = getLayoutMetrics('4x4', true, true, 16);

  assert.equal(m9No.cardW, 56);
  assert.equal(m9No.cardH, 56);
  assert.equal(m10No.cardW, 56);
  assert.equal(m12No.cardW, 56);
  assert.equal(m9Nm.cardW, 49);
  assert.equal(m9Nm.cardH, 49);
  assert.equal(m16No.cardW, 42);
  assert.equal(m16Nm.cardW, 34);

  // 2. Horizontal centering of incomplete last row in 4x4 (9, 10, 11, 12 games)
  const p9Last = computeGridSlotPos(m9No, 9, 8);
  assert.equal(p9Last.x, Math.floor((NL_SCREEN_W - m9No.cardW) / 2));

  const p10_0 = computeGridSlotPos(m10No, 10, 8);
  const p10_1 = computeGridSlotPos(m10No, 10, 9);
  const row10W = 2 * m10No.cardW + m10No.gapX;
  assert.equal(p10_0.x, Math.floor((NL_SCREEN_W - row10W) / 2));
  assert.equal(p10_1.x, p10_0.x + m10No.cardW + m10No.gapX);
  assert.equal(NL_SCREEN_W - (p10_1.x + m10No.cardW), p10_0.x);

  const p11_0 = computeGridSlotPos(m10No, 11, 8);
  const p11_2 = computeGridSlotPos(m10No, 11, 10);
  const row11W = 3 * m10No.cardW + 2 * m10No.gapX;
  assert.equal(p11_0.x, Math.floor((NL_SCREEN_W - row11W) / 2));
  assert.equal(NL_SCREEN_W - (p11_2.x + m10No.cardW), p11_0.x);

  const p12_0 = computeGridSlotPos(m12No, 12, 8);
  assert.equal(p12_0.x, m12No.marginX);

  // 3. Incomplete row centering in 3x3 (7 games), 3x2 (4 games), and 2x2 (3 games)
  const m3x3_7 = getLayoutMetrics('3x3', false, true, 7);
  assert.equal(computeGridSlotPos(m3x3_7, 7, 6).x, Math.floor((NL_SCREEN_W - m3x3_7.cardW) / 2));

  const m3x2_4 = getLayoutMetrics('3x2', false, true, 4);
  assert.equal(computeGridSlotPos(m3x2_4, 4, 3).x, Math.floor((NL_SCREEN_W - m3x2_4.cardW) / 2));

  const m2x2_3 = getLayoutMetrics('2x2', false, true, 3);
  assert.equal(computeGridSlotPos(m2x2_3, 3, 2).x, Math.floor((NL_SCREEN_W - m2x2_3.cardW) / 2));

  // 4. 4-side independent transparent margin trimming on asymmetric .ico (left=4, right=16, top=14, bottom=6 -> 44x44)
  const asymRgba = new Uint8Array(64 * 64 * 4);
  for (let y = 14; y < 58; y++) {
    for (let x = 4; x < 48; x++) {
      const o = (y * 64 + x) * 4;
      asymRgba[o + 0] = 240;
      asymRgba[o + 1] = 95;
      asymRgba[o + 2] = 35;
      asymRgba[o + 3] = 255;
    }
  }
  const asymIco = buildPngIcoBuffer(asymRgba, 64, 64);
  const decAsym = decodeWindowsIcoBuffer(asymIco);
  assert.notEqual(decAsym, null);
  assert.equal(decAsym!.sourceWidth, 46);
  assert.equal(decAsym!.sourceHeight, 46);
  assert.equal(decAsym!.width, 46);
  assert.equal(decAsym!.height, 46);
});

test('Mode 1 (Pave tactile) vs Mode 2 (Souris), CTRL temporary mouse, 3 cursor styles, non-destructive hover, universal click & persistence (#R8)', () => {
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);

  // 1. Default Mode 1 (Pave tactile): strictly cursorless (no cursor visible even if touchpadNav is ON)
  assert.equal(state.settings.touchpadMode, 'touchpad');
  assert.equal(TOUCHPAD_MODE_LABELS.touchpad, 'Pave tactile');
  assert.equal(TOUCHPAD_MODE_LABELS.mouse, 'Souris');
  assert.equal(isMouseModeActive(state), false);
  assert.equal(isTouchpadCursorVisible(state), false);
  assert.equal(state.settings.cursorSpeed, NL_CURSOR_SPEED_DEFAULT);
  assert.equal(NL_CURSOR_SPEED_COUNT, 5);
  assert.equal(CURSOR_SPEED_LABELS[2], '3/5 Normale');
  assert.deepEqual(CURSOR_STYLE_ORDER, ['arrow', 'circle', 'pointer']);
  assert.equal(CURSOR_STYLE_LABELS.arrow, 'Fleche');
  assert.equal(CURSOR_STYLE_LABELS.circle, 'Cercle');
  assert.equal(CURSOR_STYLE_LABELS.pointer, 'Pointeur');
  assert.equal(clampCursorStyle('invalid'), 'arrow');
  assert.equal(clampCursorSpeed(99), 4);

  // Even when touchpadNav=true in Mode 1, cursor remains hidden
  state = { ...state, settings: { ...state.settings, touchpadNav: true } };
  assert.equal(isTouchpadCursorVisible(state), false);

  // 2. Temporary CTRL hold activates mouse mode without overwriting saved touchpadMode='touchpad'
  state = setCtrlMouseActive(state, true);
  assert.equal(isMouseModeActive(state), true);
  assert.equal(isTouchpadCursorVisible(state), true);
  assert.equal(state.settings.touchpadMode, 'touchpad');
  // Releasing CTRL immediately returns to Mode 1 and clears hover
  state = moveMouseTo(state, 60, 80);
  assert.equal(state.sui.hoverValid, true);
  state = setCtrlMouseActive(state, false);
  assert.equal(isMouseModeActive(state), false);
  assert.equal(isTouchpadCursorVisible(state), false);
  assert.equal(state.sui.hoverValid, false);
  assert.equal(state.sui.hoverKind, 'NONE');

  // 3. Open Settings -> Navigation tactile (5 rows in Tab 0 + 3 cards in Tab 1)
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_TOUCHPAD');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.sui.tpTab, 0);
  assert.equal(state.sui.tpCursor, 0);

  // Row 0 switches between Mode 1 (Pave tactile) and Mode 2 (Souris)
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadMode, 'mouse');
  assert.equal(isMouseModeActive(state), true);
  assert.equal(isTouchpadCursorVisible(state), true);
  assert.equal(state.toastMessage, 'Mode Souris : ACTIF');

  // Row 1 toggles Glissement (Mode 1), Row 2 Sensibilite (Mode 1), Row 3 Vitesse souris (Mode 2)
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 1);
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 2);
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 3);
  state = handleLauncherKey(state, 'RIGHT'); // cursorSpeed 2 -> 3
  assert.equal(state.settings.cursorSpeed, 3);
  assert.equal(state.toastMessage, 'Vitesse souris : 4/5');
  assert.equal(state.settings.touchpadSensitivity, 2); // independent from Mode 1 sensitivity!

  // Row 4 = Tapotement, Row 5 = Taille du curseur, Row 6 opens Tab 1 ("Personnaliser le curseur")
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 4);
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 5);
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 6);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.tpTab, 1);
  assert.equal(state.sui.tpStyleCursor, 0); // 'arrow'

  // Select Cercle (RIGHT -> EXE) then Pointeur (RIGHT -> EXE)
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.sui.tpStyleCursor, 1);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadCursorStyle, 'circle');
  assert.equal(state.toastMessage, 'Curseur : Cercle');

  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.sui.tpStyleCursor, 2);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadCursorStyle, 'pointer');
  assert.equal(state.toastMessage, 'Curseur : Pointeur');

  // TAB switches back to Tab 0, ESC returns to LIBRARY
  state = handleLauncherKey(state, 'TAB');
  assert.equal(state.sui.tpTab, 0);
  state = handleLauncherKey(state, 'ESC');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'LIBRARY');

  // 4. Hardware Y-axis inversion on feedMouseTouchpadDelta & [0..319]x[0..239] clamping
  state = moveMouseTo(state, 160, 120);
  const tracker = createTouchpadTracker();
  let res = feedMouseTouchpadDelta(state, tracker, true, 1000, 800, 2328, 1691, true);
  assert.equal(res.moved, false); // contact start
  const startY = res.state.sui.mouseY;
  // Moving finger UP increases hardware rawY -> decreases screen mouseY
  res = feedMouseTouchpadDelta(res.state, tracker, true, 1250, 1050, 2328, 1691, true);
  assert.equal(res.moved, true);
  assert.equal(res.state.sui.mouseX > 160, true);
  assert.equal(res.state.sui.mouseY < startY, true);
  // Clamping at boundaries
  const clampedHigh = moveMouseTo(res.state, 999, 999);
  assert.equal(clampedHigh.sui.mouseX, NL_SCREEN_W - 1);
  assert.equal(clampedHigh.sui.mouseY, NL_SCREEN_H - 1);

  // 5. Hovering cards & top controls is strictly non-destructive (NEVER auto-launches games!)
  state = { ...res.state, settings: { ...res.state.settings, layout: '3x2' }, selectedPos: 0, currentPage: 0 };
  const c0Target = computeTouchpadCursorTarget({ ...state, settings: { ...state.settings, touchpadMode: 'touchpad' }, sui: { ...state.sui, ctrlMouseActive: false } });
  const hoveredCard = moveMouseTo(state, c0Target.x, c0Target.y);
  assert.equal(hoveredCard.sui.hoverKind, 'GAME_CARD');
  assert.equal(hoveredCard.sui.hoverIndex, 0);
  assert.equal(hoveredCard.runningGameIndex, null); // Never auto-launched on hover!

  // Clicking FAV button (x=156, y=12) toggles favorite on selected game
  const favHover = moveMouseTo(hoveredCard, 156, 12);
  assert.equal(favHover.sui.hoverKind, 'FAV_BTN');
  const prevFav = favHover.games[favHover.filteredIndices[favHover.selectedPos]].isFavorite;
  const favClicked = clickMouse(favHover);
  assert.equal(favClicked.handled, true);
  assert.equal(
    favClicked.state.games[favClicked.state.filteredIndices[favClicked.state.selectedPos]].isFavorite,
    !prevFav
  );

  // Clicking category tab FAV (x=90, y=12) switches category
  const catHover = moveMouseTo(favClicked.state, 90, 12);
  assert.equal(catHover.sui.hoverKind, 'CATEGORY');
  assert.equal(catHover.sui.hoverIndex, 1);
  const catClicked = clickMouse(catHover);
  assert.equal(catClicked.handled, true);
  assert.equal(catClicked.state.activeCategory, 'FAVORITES');

  // Switch back to TOUS (x=52, y=12) then click card 0 to launch
  const allHover = moveMouseTo(catClicked.state, 52, 12);
  assert.equal(allHover.sui.hoverKind, 'CATEGORY');
  assert.equal(allHover.sui.hoverIndex, 0);
  const allClicked = clickMouse(allHover).state;
  assert.equal(allClicked.activeCategory, 'ALL');
  const cardClick = clickMouse(moveMouseTo(allClicked, c0Target.x, c0Target.y));
  assert.equal(cardClick.handled, true);
  assert.equal(cardClick.state.runningGameIndex, cardClick.state.filteredIndices[0]);
  state = handleLauncherKey(cardClick.state, 'ESC');
  assert.equal(state.runningGameIndex, null);

  // 6. Direct Mode/Speed/Style canvas helpers & Persistence across rescan & safe reset
  const inSettings = moveMouseTo(
    handleLauncherKey(handleLauncherKey(handleLauncherKey(state, 'SETTINGS'), 'DOWN'), 'DOWN'),
    160,
    92
  );
  const onTpScreen = handleLauncherKey(inSettings, 'EXE');
  const directCtrl = setTouchpadControl(onTpScreen, 3, 4, true, 1, 'circle', 0, false, 'mouse');
  assert.equal(directCtrl.settings.touchpadMode, 'mouse');
  assert.equal(directCtrl.settings.touchpadNav, false);
  assert.equal(directCtrl.settings.touchpadSensitivity, 4);
  assert.equal(directCtrl.settings.cursorSpeed, 1);
  assert.equal(directCtrl.settings.touchpadCursorStyle, 'circle');

  const rescanned = scanVirtualFilesystem(files, '/documents/ndless/N-Launcher.tns', state.games, state.playCounter, {
    displayMode: directCtrl.displayMode,
    accentColor: directCtrl.accentColor,
    settings: directCtrl.settings,
  });
  assert.equal(rescanned.settings.touchpadMode, 'mouse');
  assert.equal(rescanned.settings.cursorSpeed, 1);
  assert.equal(rescanned.settings.touchpadCursorStyle, 'circle');

  const afterReset = resetSettingsOnly(rescanned);
  assert.equal(afterReset.settings.touchpadMode, 'touchpad');
  assert.equal(afterReset.settings.cursorSpeed, NL_CURSOR_SPEED_DEFAULT);
  assert.equal(afterReset.settings.touchpadCursorStyle, 'arrow');
  assert.equal(isTouchpadCursorVisible(afterReset), false);
});

test('deliverable N-Launcher.tns synchronization, Build 2026-11-18 #R23 stamp, Zehn allocSize, and SHA-256 verification', async () => {
  const {
    EXPECTED_TNS_BUILD_STAMP,
    EXPECTED_TNS_SHA256,
    EXPECTED_TNS_SIZE_BYTES,
    computeSha256Hex,
    verifyTnsBuffer,
  } = await import('../utils/downloadTns');

  assert.equal(EXPECTED_TNS_BUILD_STAMP, 'Build 2026-11-18 #R23');
  assert.equal(EXPECTED_TNS_SIZE_BYTES, 579004);

  const pathsToCheck = [
    path.resolve(process.cwd(), 'native/N-Launcher.tns'),
    path.resolve(process.cwd(), 'N-Launcher.tns'),
    path.resolve(process.cwd(), 'public/N-Launcher.tns'),
    path.resolve(process.cwd(), 'public/deliverables/N-Launcher.tns'),
  ];

  for (const p of pathsToCheck) {
    assert.equal(fs.existsSync(p), true, `Missing deliverable binary at ${p}`);
    const buf = fs.readFileSync(p);
    const ab = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
    const check = verifyTnsBuffer(ab, 'N-Launcher.tns');
    assert.equal(check.valid, true);
    assert.equal(check.sizeBytes, EXPECTED_TNS_SIZE_BYTES);
    assert.equal(check.allocSize, 627116);
    assert.equal(check.relocCount, 2893);
    assert.equal(check.buildStamp, EXPECTED_TNS_BUILD_STAMP);
    assert.equal(check.isLatestBuild, true);
    const sha = await computeSha256Hex(ab);
    assert.equal(sha, EXPECTED_TNS_SHA256);
  }
});

test('CX II-T 6.2.0 diagnostic system: 12 boot stages, fault isolation, deduplication, OOM recovery, and SETTINGS_DIAG keyboard + mouse UI (#R9)', () => {
  // 1. Default reference configuration is TI-Nspire CX II-T (OS 6.2.0) and toggleable to CX CAS 4.5.0
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);
  assert.equal(state.sys.hwIsCx2, true);
  assert.equal(state.sys.osVersion.startsWith('6.2.0'), true);
  assert.equal(state.settings.autostart, false);
  assert.equal(state.settings.showBattery, false);

  state = setHardwarePreset(state, 'CXCAS_450');
  assert.equal(state.sys.hwIsCx2, false);
  assert.equal(state.sys.osVersion.startsWith('4.5.0'), true);
  state = setHardwarePreset(state, 'CX2T_620');
  assert.equal(state.sys.hwIsCx2, true);

  // 2. 12-stage boot forensics labels & recording
  assert.equal(NL_BOOT_STAGE_COUNT, 12);
  assert.match(getBootStageLabel(1), /Entree/);
  assert.match(getBootStageLabel(12), /Sortie propre/);
  state = recordBootStage(state, 12, true);
  assert.equal(state.diag.currentStage, 12);
  assert.equal(state.diag.stageReached[12], true);

  // 3. Fault isolation during virtual filesystem scan: corrupt .tns and corrupt .ico.tns do NOT block valid games
  const badExeBytes = new Uint8Array([0x00, 0x00, 0xff, 0xff, 0x12, 0x34, 0x56, 0x78]);
  const badIcoBytes = new Uint8Array([0x00, 0x00, 0x01, 0x00, 0x99, 0x99]);
  const baseGamesCount = state.games.length;
  const firstStem = extractGameStem(state.games[0].filename).toLowerCase();
  const faultFiles = [
    ...files,
    { path: '/documents/games/broken_game.tns', sizeBytes: badExeBytes.length, headerBytes: badExeBytes },
    { path: `/documents/ndless/icons/${firstStem}.ico.tns`, sizeBytes: badIcoBytes.length, headerBytes: badIcoBytes },
  ];
  let isolated = scanVirtualFilesystem(faultFiles);
  assert.equal(isolated.games.length, baseGamesCount);
  assert.equal(
    isolated.diag.entries.some((e) => e.code === 'E201' && e.path.includes('broken_game.tns')),
    true
  );
  assert.equal(
    isolated.diag.entries.some((e) => e.code === 'E103' && e.path.includes(`${firstStem}.ico.tns`)),
    true
  );

  // 4. Error deduplication (same code + path increments count rather than spamming duplicates)
  isolated = reportDiagError(
    isolated,
    'E102',
    'Minecraft',
    '/documents/ndless/icons/minecraft.ico.tns',
    'Chargement icone'
  );
  isolated = reportDiagError(
    isolated,
    'E102',
    'Minecraft',
    '/documents/ndless/icons/minecraft.ico.tns',
    'Chargement icone'
  );
  const e102 = isolated.diag.entries.find((e) => e.code === 'E102');
  assert.ok(e102);
  assert.equal(e102.count, 2);
  assert.equal(e102.title, getDiagDefaults('E102').title);

  // 5. OOM recovery & fault injection scenarios
  isolated = simulateFaultInjection(isolated, 'OOM');
  assert.equal(isolated.sys.oomRecoveries, 1);
  assert.equal(
    isolated.diag.entries.some((e) => e.code === 'E801'),
    true
  );
  isolated = recoverMemoryOnOom(isolated, 'Cache decompression');
  assert.equal(isolated.sys.oomRecoveries, 2);

  isolated = simulateFaultInjection(isolated, 'PREV_CRASH');
  assert.equal(isolated.sys.priorIncompleteBoot, true);
  assert.equal(
    isolated.diag.entries.some((e) => e.code === 'E901'),
    true
  );

  // 6. SETTINGS_DIAG keyboard navigation, detail modal, Tab 1 boot stages, and safe confirm modal (default Annuler)
  const diagMenuIdx = SETTINGS_MENU_ITEMS.findIndex((i) => i.id === 'SETTINGS_DIAG');
  assert.ok(diagMenuIdx >= 0);
  let uiState = handleLauncherKey(isolated, 'SETTINGS');
  for (let i = 0; i < diagMenuIdx; i++) {
    uiState = handleLauncherKey(uiState, 'DOWN');
  }
  uiState = handleLauncherKey(uiState, 'EXE');
  assert.equal(uiState.sui.screen, 'SETTINGS_DIAG');
  assert.equal(uiState.sui.diagTab, 0);

  // Open detail on entry 0
  uiState = handleLauncherKey(uiState, 'EXE');
  assert.equal(uiState.sui.diagDetailOpen, true);
  uiState = handleLauncherKey(uiState, 'ESC');
  assert.equal(uiState.sui.diagDetailOpen, false);

  // Switch to Tab 1 (Boot & Systeme) & scroll
  uiState = handleLauncherKey(uiState, 'TAB');
  assert.equal(uiState.sui.diagTab, 1);
  uiState = handleLauncherKey(uiState, 'DOWN');
  assert.equal(uiState.sui.diagScroll, 1);
  uiState = handleLauncherKey(uiState, 'TAB');
  assert.equal(uiState.sui.diagTab, 0);

  // 7. Mode 2 Souris interaction inside SETTINGS_DIAG & safe clearing
  uiState = {
    ...uiState,
    settings: { ...uiState.settings, touchpadMode: 'mouse', touchpadCursorShow: true },
  };
  // Verify SET_POWER is disabled on SETTINGS_DIAG top bar
  uiState = moveMouseTo(uiState, 160, 12);
  assert.notEqual(uiState.sui.hoverKind, 'SET_POWER');

  // Hover & click Tab 1 then back to Tab 0 via mouse
  uiState = moveMouseTo(uiState, 220, 35);
  assert.equal(uiState.sui.hoverKind, 'DIAG_TAB');
  assert.equal(uiState.sui.hoverIndex, 1);
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.sui.diagTab, 1);
  uiState = moveMouseTo(uiState, 80, 35);
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.sui.diagTab, 0);

  // Hover & click row 0 to open detail, then click detail card to close
  uiState = moveMouseTo(uiState, 120, 78);
  assert.equal(uiState.sui.hoverKind, 'DIAG_ROW');
  assert.equal(uiState.sui.hoverIndex, 0);
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.sui.diagDetailOpen, true);
  uiState = moveMouseTo(uiState, 160, 120);
  assert.equal(uiState.sui.hoverKind, 'DIAG_DETAIL_CLOSE');
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.sui.diagDetailOpen, false);

  // Hover & click Clear button -> confirm modal defaults to Annuler (diagConfirmYes=false)
  const gamesBeforeClear = uiState.games.length;
  uiState = moveMouseTo(uiState, 160, 204);
  assert.equal(uiState.sui.hoverKind, 'DIAG_CLEAR_BTN');
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.sui.diagConfirmModal, true);
  assert.equal(uiState.sui.diagConfirmYes, false);

  // Clicking Annuler closes without wiping logs
  uiState = moveMouseTo(uiState, 90, 164);
  assert.equal(uiState.sui.hoverKind, 'MODAL_BTN');
  assert.equal(uiState.sui.hoverIndex, 0);
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.sui.diagConfirmModal, false);
  assert.equal(uiState.diag.entries.length > 0, true);

  // Reopen confirm modal & click Effacer -> clears diagnostic logs while keeping games & settings intact
  uiState = handleLauncherKey(uiState, 'RESCAN');
  assert.equal(uiState.sui.diagConfirmModal, true);
  uiState = moveMouseTo(uiState, 220, 164);
  assert.equal(uiState.sui.hoverKind, 'MODAL_BTN');
  assert.equal(uiState.sui.hoverIndex, 1);
  uiState = clickMouse(uiState).state;
  assert.equal(uiState.diag.entries.length, 0);
  assert.equal(uiState.games.length, gamesBeforeClear);
  assert.equal(uiState.settings.touchpadMode, 'mouse');

  const afterDirectClear = clearDiagLogs(isolated);
  assert.equal(afterDirectClear.diag.entries.length, 0);
});

test('A-to-Z audit regressions: .bak.tns exclusion, SETTINGS_DISPLAY 31px stride & SETTINGS_CAPTURE_BATCH mouse hitboxes', () => {
  // 1. .bak.tns backup files are treated as system/internal files and never trigger false E201
  assert.equal(isSystemOrSelfFile('/documents/ndless/nlauncher.cfg.bak.tns'), true);
  assert.equal(isSystemOrSelfFile('/documents/ndless/nlauncher.profiles.bak.tns'), true);

  // 2. SETTINGS_DISPLAY 31px stride mouse hover & click parity
  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  state = {
    ...state,
    settings: { ...state.settings, touchpadMode: 'mouse', touchpadCursorShow: true },
    sui: { ...state.sui, screen: 'SETTINGS_DISPLAY', displayCursor: 0, displayScroll: 0 },
  };
  // Row 2 is at ry = 30 + 2 * 31 = 92..120
  state = moveMouseTo(state, 160, 92 + 14);
  assert.equal(state.sui.hoverValid, true);
  assert.equal(state.sui.hoverKind, 'SET_ROW');
  assert.equal(state.sui.hoverIndex, 2);

  // 3. SETTINGS_CAPTURE_BATCH mouse hitboxes match C renderer (30..154 / 166..290 at y=174..198)
  state = {
    ...state,
    sui: {
      ...state.sui,
      screen: 'SETTINGS_CAPTURE_BATCH',
      capActive: true,
      capTotal: 3,
      capPos: 0,
      capQueue: [0, 1, 2],
    },
  };
  state = moveMouseTo(state, 80, 185);
  assert.equal(state.sui.hoverKind, 'SET_ROW');
  assert.equal(state.sui.hoverIndex, 0);
  state = moveMouseTo(state, 220, 185);
  assert.equal(state.sui.hoverKind, 'SET_ROW');
  assert.equal(state.sui.hoverIndex, 1);
});

test('#R11 Mouse Mode finalization: 3 independent cursor sizes x 3 models, tap-to-click toggle, 7-row scrollable Tab 0 & 2-row Tab 1', () => {
  assert.deepEqual(CURSOR_SIZE_ORDER, ['small', 'medium', 'large']);
  assert.equal(CURSOR_SIZE_LABELS.small, 'Petite');
  assert.equal(CURSOR_SIZE_LABELS.medium, 'Moyenne');
  assert.equal(CURSOR_SIZE_LABELS.large, 'Grande');
  assert.equal(clampCursorSize('unknown'), NL_CURSOR_SIZE_DEFAULT);

  // 1. Verify strictly increasing proportional dimensions & valid hotspots for all 3 models x 3 sizes
  for (const style of CURSOR_STYLE_ORDER) {
    let prevW = 0;
    let prevH = 0;
    for (const sz of CURSOR_SIZE_ORDER) {
      const m = getCursorSizeMetrics(style, sz);
      assert.equal(m.width > prevW, true);
      assert.equal(m.height > prevH, true);
      assert.equal(m.hotX >= 0 && m.hotX < m.width, true);
      assert.equal(m.hotY >= 0 && m.hotY < m.height, true);
      prevW = m.width;
      prevH = m.height;
    }
  }

  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);
  assert.equal(state.settings.cursorSize, 'medium');
  assert.equal(state.settings.tapToClick, false);

  // 2. Open Settings -> Navigation tactile (7 scrollable rows in Tab 0, 5 visible)
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.sui.tpTab, 0);
  assert.equal(getTouchpadTab0RowRects(0).length, 5);
  assert.equal(getTouchpadTab0RowRects(2)[4].row, 6);

  // Scroll down to row 6 -> tpScroll reaches 2
  for (let r = 1; r <= 6; r++) {
    state = handleLauncherKey(state, 'DOWN');
    assert.equal(state.sui.tpCursor, r);
  }
  assert.equal(state.sui.tpScroll, 2);

  // Row 5 (Taille du curseur): medium -> large, strictly independent from model/speed/tapToClick
  state = handleLauncherKey(state, 'UP');
  assert.equal(state.sui.tpCursor, 5);
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.cursorSize, 'large');
  assert.equal(state.settings.touchpadCursorStyle, 'arrow');
  assert.equal(state.settings.cursorSpeed, 2);
  assert.equal(state.settings.tapToClick, false);

  // Row 4 (Tapotement pour cliquer): toggle OUI -> NON -> OUI without altering size
  state = handleLauncherKey(state, 'UP');
  assert.equal(state.sui.tpCursor, 4);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.tapToClick, true);
  assert.equal(state.toastMessage, 'Tapotement pour cliquer : OUI');
  assert.equal(state.settings.cursorSize, 'large');

  // 3. Switch to Tab 1 (2 rows: Model cards + Size cards) and test mouse clicks + keyboard
  state = {
    ...state,
    settings: { ...state.settings, touchpadMode: 'mouse', touchpadCursorShow: true },
  };
  state = handleLauncherKey(state, 'TAB');
  assert.equal(state.sui.tpTab, 1);

  // Click Pointer model (card 2, Row 0) -> preserves 'large' size!
  state = moveMouseTo(state, 14 + 2 * 101 + 45, 90);
  assert.equal(state.sui.hoverKind, 'SET_STYLE_CARD');
  assert.equal(state.sui.hoverIndex, 2);
  state = clickMouse(state).state;
  assert.equal(state.settings.touchpadCursorStyle, 'pointer');
  assert.equal(state.settings.cursorSize, 'large');

  // Click Small size (card 0, Row 1) -> preserves 'pointer' model!
  state = moveMouseTo(state, 14 + 0 * 101 + 45, 175);
  assert.equal(state.sui.hoverKind, 'SET_SIZE_CARD');
  assert.equal(state.sui.hoverIndex, 0);
  state = clickMouse(state).state;
  assert.equal(state.settings.cursorSize, 'small');
  assert.equal(state.settings.touchpadCursorStyle, 'pointer');

  // 4. Persistence across rescan & clean reset to defaults
  const reloaded = scanVirtualFilesystem(files, '/documents/ndless/N-Launcher.tns', state.games, state.playCounter, {
    displayMode: state.displayMode,
    accentColor: state.accentColor,
    settings: state.settings,
  });
  assert.equal(reloaded.settings.touchpadCursorStyle, 'pointer');
  assert.equal(reloaded.settings.cursorSize, 'small');
  assert.equal(reloaded.settings.tapToClick, true);

  const reset = resetSettingsOnly(reloaded);
  assert.equal(reset.settings.touchpadCursorStyle, 'arrow');
  assert.equal(reset.settings.cursorSize, 'medium');
  assert.equal(reset.settings.tapToClick, false);
});

test('#R12 Decoupled icons, repeatable SETTINGS_RESCAN summary & empty-scan confirmation guard', () => {
  // 1. Delete all icons -> all games remain present with clean monogram fallback
  const filesWithIcons = createDefaultSimulatorFilesystem();
  const filesWithoutIcons = filesWithIcons.filter(
    (f) => !f.path.toLowerCase().includes('/icons/') && !f.path.toLowerCase().endsWith('.ico.tns')
  );
  let state = scanVirtualFilesystem(filesWithoutIcons);
  assert.equal(state.games.length > 0, true);
  for (const g of state.games) {
    assert.equal(g.iconSource, 'NONE');
  }

  // 2. Open Settings -> Rescanner les jeux -> repeatable rescan works cleanly
  state = handleLauncherKey(state, 'SETTINGS');
  const idx = SETTINGS_MENU_ITEMS.findIndex((i) => i.id === 'SETTINGS_REFRESH_LIB');
  assert.equal(idx >= 0, true);
  for (let i = 0; i < idx; i++) {
    state = handleLauncherKey(state, 'DOWN');
  }
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_RESCAN');
  assert.equal(state.sui.rescan.rescanCount, 1);
  assert.equal(state.sui.rescan.gamesFound, state.games.length);
  assert.equal(state.sui.rescan.iconsFallback, state.games.length);
  assert.equal(state.sui.rescan.memorizedFolders >= 1, true);

  // Press DEL/RESCAN to trigger second rescan in place
  state = handleLauncherKey(state, 'RESCAN');
  assert.equal(state.sui.screen, 'SETTINGS_RESCAN');
  assert.equal(state.sui.rescan.rescanCount, 2);

  // 3. Empty-scan confirmation guard: never auto-wipes existing games!
  const beforeCount = state.games.length;
  state = runSimulatorRescan(state, { simulatedEmpty: true });
  assert.equal(state.sui.screen, 'SETTINGS_RESCAN');
  assert.equal(state.sui.rescan.emptyConfirmPending, true);
  assert.equal(state.sui.rescan.emptyConfirmReplace, false);
  assert.equal(state.games.length, beforeCount);

  // Default choice is Conserver -> pressing EXE preserves all games
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.rescan.emptyConfirmPending, false);
  assert.equal(state.games.length, beforeCount);
  assert.equal(state.toastMessage, 'BIBLIOTHEQUE CONSERVEE');

  // Re-trigger empty guard and choose Vider via mouse click on button 1 (x=220, y=178)
  state = {
    ...runSimulatorRescan(state, { simulatedEmpty: true }),
    settings: { ...state.settings, touchpadMode: 'mouse', touchpadCursorShow: true },
  };
  state = moveMouseTo(state, 220, 178);
  assert.equal(state.sui.hoverKind, 'MODAL_BTN');
  assert.equal(state.sui.hoverIndex, 1);
  state = clickMouse(state).state;
  assert.equal(state.games.length, 0);
  assert.equal(state.toastMessage, 'BIBLIOTHEQUE VIDEE');
});

test('#R15 UX overhaul: 0/- page navigation, Vertical/Horizontal scrollDir & padlock profile protection', () => {
  let state = scanVirtualFilesystem(createDefaultSimulatorFilesystem());
  assert.equal(state.totalPages >= 2, true);
  assert.equal(state.settings.scrollDir, 'vertical');
  assert.equal(state.settings.profileLockEnabled, false);

  // 1. Reassigned 0 and - keys navigate pages (never switching profiles)
  const p2 = createUserProfile(state, 'LockedUser', 7);
  state = p2.state;
  const activeBefore = state.profiles.activeIndex;
  assert.equal(state.currentPage, 0);
  state = handleCharInput(state, '-');
  assert.equal(state.currentPage, 1);
  assert.equal(state.profiles.activeIndex, activeBefore);
  state = handleCharInput(state, '0');
  assert.equal(state.currentPage, 0);
  assert.equal(state.profiles.activeIndex, activeBefore);

  // 2. Scroll direction toggle in Settings -> Affichage (row 1)
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'EXE'); // SETTINGS_DISPLAY
  state = handleLauncherKey(state, 'DOWN'); // row 1: Sens de defilement
  assert.equal(state.sui.displayCursor, 1);
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.scrollDir, 'horizontal');
  state = handleLauncherKey(state, 'ESC');
  state = handleLauncherKey(state, 'ESC');

  // 3. Padlock protection: global toggle + per-profile lock guards
  assert.equal(isProfileProtected(state, activeBefore), false);
  state = { ...state, settings: { ...state.settings, profileLockEnabled: true } };
  const toggled = toggleProfileLock(state, activeBefore);
  assert.equal(toggled.ok, true);
  assert.equal(toggled.isLocked, true);
  state = toggled.state;
  assert.equal(isProfileProtected(state, activeBefore), true);

  // Locked profile blocks rename, avatar change, delete, favorite toggle
  assert.equal(renameUserProfile(state, activeBefore, 'Hacked').ok, false);
  assert.equal(setUserProfileAvatar(state, activeBefore, 12).ok, false);
  assert.equal(deleteUserProfile(state, activeBefore).ok, false);
  const favBefore = state.games[state.filteredIndices[0]].isFavorite;
  state = handleLauncherKey(state, 'FAV');
  assert.equal(state.games[state.filteredIndices[0]].isFavorite, favBefore);

  // Disabling global profileLockEnabled removes all lock restrictions
  state = { ...state, settings: { ...state.settings, profileLockEnabled: false } };
  assert.equal(isProfileProtected(state, activeBefore), false);
  assert.equal(renameUserProfile(state, activeBefore, 'UnlockedName').ok, true);
});

test('#R17 Exact R15 Horizontal side-peeks + mirrored Vertical top/bottom natural icon peeks (zero carousels, click-to-page without launch) & Orbital Prism tracking', () => {
  let state = scanVirtualFilesystem(createDefaultSimulatorFilesystem());
  assert.equal(state.totalPages >= 2, true);

  // 1. Verify ALL 5 layouts in both Horizontal (R15 exact 12px side peeks) & Vertical (R17 mirrored 12px top/bottom peeks)
  for (const lay of LAYOUT_ORDER) {
    const lmH = getLayoutMetrics(lay, true, true, 32, 'horizontal');
    const lmV = getLayoutMetrics(lay, true, true, 32, 'vertical');
    const lmVS = getLayoutMetrics(lay, true, true, 1, 'vertical');

    // Horizontal mode: R15 exact 12px left/right side peeks, full-height 3x2 grid (no top/bottom carousel bands)
    assert.equal(lmH.isHorizontal, true);
    assert.equal(lmH.marginX >= 28, true);
    assert.equal(lmH.marginX + lmH.pageStrideX <= 310, true);
    if (lay === '3x2') {
      assert.equal(lmH.topY < 44, true);
    }

    // Vertical multi-page mode: natural 12px top & bottom real icon peeks + >= 8px inter-page gap
    assert.equal(lmV.isHorizontal, false);
    assert.equal(lmV.topY >= 46, true);
    const vContentH = lmV.rows * (lmV.cardH + lmV.labelH) + (lmV.rows - 1) * lmV.gapY;
    assert.equal(lmV.topY + lmV.pageStrideY, lmV.viewBottom - 12);
    assert.equal(lmV.pageStrideY >= vContentH + 8, true);
    const vTopPeekEnd = lmV.topY + vContentH - lmV.pageStrideY;
    assert.equal(vTopPeekEnd >= 36 && vTopPeekEnd <= 39, true);

    // Single-page vertical stays full-size (strictly larger than multi-page vertical peek)
    assert.equal(lmVS.cardH > lmV.cardH, true);
  }

  // 2. Vertical Mode interactive click on bottom peeking icon (Page 1) & top peeking icon (Page 0) WITHOUT launching
  state = {
    ...state,
    settings: {
      ...state.settings,
      layout: '3x2',
      scrollDir: 'vertical',
      touchpadMode: 'mouse',
      touchpadCursorShow: true,
    },
    currentPage: 0,
    runningGameIndex: null,
  };
  const vlm = getLayoutMetrics('3x2', state.settings.showNames, state.settings.showShortcuts, state.filteredIndices.length, 'vertical');
  const p1Slot0 = computeGridSlotPos(vlm, state.filteredIndices.length, vlm.perPage);
  const botProbeX = p1Slot0.x + Math.floor(vlm.cardW / 2);

  state = moveMouseTo(state, botProbeX, vlm.viewBottom - 6);
  assert.equal(state.sui.hoverKind, 'PAGE_NEXT');
  const vNextClick = clickMouse(state);
  assert.equal(vNextClick.handled, true);
  state = vNextClick.state;
  assert.equal(state.currentPage, 1);
  assert.equal(state.runningGameIndex, null);

  // Now on Page 1: click top peeking icon from Page 0 -> jumps back to Page 0 WITHOUT launching
  const p0Slot3 = computeGridSlotPos(vlm, state.filteredIndices.length, 3);
  const topProbeX = p0Slot3.x + Math.floor(vlm.cardW / 2);
  state = moveMouseTo(state, topProbeX, 31);
  assert.equal(state.sui.hoverKind, 'PAGE_PREV');
  const vPrevClick = clickMouse(state);
  assert.equal(vPrevClick.handled, true);
  state = vPrevClick.state;
  assert.equal(state.currentPage, 0);
  assert.equal(state.runningGameIndex, null);

  // 3. Horizontal Mode (R15 exact): click right/left peeking icons -> page jump WITHOUT launching
  state = {
    ...state,
    settings: { ...state.settings, scrollDir: 'horizontal' },
    currentPage: 0,
    runningGameIndex: null,
  };
  const hlm = getLayoutMetrics('3x2', state.settings.showNames, state.settings.showShortcuts, state.filteredIndices.length, 'horizontal');
  const hP1Slot0 = computeGridSlotPos(hlm, state.filteredIndices.length, hlm.perPage);
  state = moveMouseTo(state, 314, hP1Slot0.y + 16);
  assert.equal(state.sui.hoverKind, 'PAGE_NEXT');
  const hNextClick = clickMouse(state);
  assert.equal(hNextClick.handled, true);
  state = hNextClick.state;
  assert.equal(state.currentPage, 1);
  assert.equal(state.runningGameIndex, null);

  state = moveMouseTo(state, 6, hlm.topY + 16);
  assert.equal(state.sui.hoverKind, 'PAGE_PREV');
  const hPrevClick = clickMouse(state);
  assert.equal(hPrevClick.handled, true);
  state = hPrevClick.state;
  assert.equal(state.currentPage, 0);
  assert.equal(state.runningGameIndex, null);

  // 4. Profile switch records both outgoing (profSwitchFromAvatarId) & incoming (profSwitchAvatarId) without launching a game
  const created = createUserProfile(state, 'PrismUser', 14);
  state = created.state;
  const fromAv = state.profiles.items[0].avatarId;
  const toAv = state.profiles.items[1].avatarId;
  state = switchProfileTo(state, 0);
  state = handleCharInput(state, '.');
  assert.equal(state.sui.qpOpen, true);
  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.qpOpen, false);
  assert.equal(state.runningGameIndex, null);
  assert.equal(state.profiles.activeIndex, 1);
  assert.equal(state.profSwitchFromAvatarId, fromAv);
  assert.equal(state.profSwitchAvatarId, toAv);
});

test('#R20 Settings -> Animations -> 4 launch animations, persistence across profiles/rescan & reset', () => {
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);
  assert.equal(state.settings.launchAnimMode, 'current');

  // Open Settings -> Animations (index 1)
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(state.sui.screen, 'SETTINGS_MAIN');
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_ANIMATIONS');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_ANIMATIONS');
  assert.equal(state.sui.animCursor, 0);

  // Navigate to Row 2 (Lancement des jeux) and cycle Theme Drop -> Spinner -> Loading Bar
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.animCursor, 2);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.launchAnimMode, 'theme_drop');

  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.launchAnimMode, 'spinner');

  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.launchAnimMode, 'loading_bar');

  // Persists across rescan and profile reload
  const reloaded = scanVirtualFilesystem(
    files,
    '/documents/ndless/N-Launcher.tns',
    state.games,
    state.playCounter,
    {
      displayMode: state.displayMode,
      accentColor: state.accentColor,
      settings: state.settings,
      profiles: state.profiles,
    }
  );
  assert.equal(reloaded.settings.launchAnimMode, 'loading_bar');

  // Safe reset restores default Animation 1 ('current')
  const afterReset = resetSettingsOnly(reloaded);
  assert.equal(afterReset.settings.launchAnimMode, 'current');
});

test('#R21 Fast game return emits non-blocking returnDropSeq across 5 launches and keeps library immediately interactive', () => {
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);
  assert.equal(state.returnDropSeq ?? 0, 0);

  for (let i = 0; i < 5; i++) {
    const prevSeq = state.returnDropSeq ?? 0;
    const expectedGameIdx = state.filteredIndices[state.selectedPos];
    state = handleLauncherKey(state, 'EXE');
    assert.equal(state.runningGameIndex, expectedGameIdx);

    // Exit game with ESC -> instant return to library + return droplet trigger
    state = handleLauncherKey(state, 'ESC', Math.floor(Date.now() / 1000), 12);
    assert.equal(state.runningGameIndex, null);
    assert.equal(state.returnDropSeq, prevSeq + 1);
    assert.equal(state.returnDropGameIdx, expectedGameIdx);

    // Library MUST be immediately navigable right after return (zero blocking wait!)
    const posBefore = state.selectedPos;
    state = handleLauncherKey(state, 'RIGHT');
    assert.notEqual(state.selectedPos, posBefore);
  }

  // Opening Quick Profile & '+' button still triggers profPlusSeq cleanly
  state = handleLauncherKey(state, 'PROFILE_QUICK');
  assert.equal(state.sui.qpOpen, true);
});

test('#R22 Organic liquid propagation labels, build stamp, and profile open/close/switch/+ state flow', async () => {
  const {
    LAUNCH_ANIM_SHORT_LABELS,
    LAUNCH_ANIM_SUBTITLES,
    NL_BUILD_STAMP_STR,
  } = await import('./nlauncherEngine');

  assert.equal(NL_BUILD_STAMP_STR, 'Build 2026-11-18 #R23');
  assert.equal(LAUNCH_ANIM_SHORT_LABELS.theme_drop, 'LIQUIDE');
  assert.equal(LAUNCH_ANIM_SUBTITLES.theme_drop, '4 propagations liquides');

  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);

  // Select Theme Drop launch mode via Settings -> Animations (Row 2)
  state = handleLauncherKey(state, 'SETTINGS');
  state.sui.screen = 'SETTINGS_ANIMATIONS';
  state.sui.animCursor = 2; // Lancement des jeux
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.launchAnimMode, 'theme_drop');

  // Return to library & launch/return cycle
  state = handleLauncherKey(state, 'ESC');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'LIBRARY');
  state = handleLauncherKey(state, 'EXE');
  assert.notEqual(state.runningGameIndex, null);
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.runningGameIndex, null);
  assert.equal((state.returnDropSeq ?? 0) >= 1, true);

  // Quick Profile Open -> dedicated Close (ESC) -> Re-open cleanly
  state = handleLauncherKey(state, 'PROFILE_QUICK');
  assert.equal(state.sui.qpOpen, true);
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.qpOpen, false);
});

test('#R23 4 independent animation categories (App Open / App Close / Game Launch / Game Return), persistence & fast return filter guard', async () => {
  const { returnNeedsFilterRebuild } = await import('./nlauncherEngine');
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);

  // Defaults
  assert.equal(state.settings.appOpenAnim, 'current');
  assert.equal(state.settings.appCloseAnim, 'liquid');
  assert.equal(state.settings.launchAnimMode, 'current');
  assert.equal(state.settings.returnAnimMode, 'liquid');

  // Conditional return filter rebuild guard
  assert.equal(returnNeedsFilterRebuild(state), false);
  assert.equal(
    returnNeedsFilterRebuild({ ...state, settings: { ...state.settings, sortOrder: 'last_used' } }),
    true
  );
  assert.equal(
    returnNeedsFilterRebuild({ ...state, activeCategory: 'RECENT' }),
    true
  );

  // Open Settings -> Animations and verify strict independence across all 4 rows
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_ANIMATIONS');
  assert.equal(state.sui.animCursor, 0);

  // Row 0 (App Open) -> zoom; rows 1/2/3 untouched
  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.appOpenAnim, 'zoom');
  assert.equal(state.settings.appCloseAnim, 'liquid');
  assert.equal(state.settings.launchAnimMode, 'current');
  assert.equal(state.settings.returnAnimMode, 'liquid');

  // Row 1 (App Close) -> fade; rows 0/2/3 untouched
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.appOpenAnim, 'zoom');
  assert.equal(state.settings.appCloseAnim, 'fade');
  assert.equal(state.settings.launchAnimMode, 'current');
  assert.equal(state.settings.returnAnimMode, 'liquid');

  // Row 2 (Game Launch) -> spinner; rows 0/1/3 untouched
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.settings.appOpenAnim, 'zoom');
  assert.equal(state.settings.appCloseAnim, 'fade');
  assert.equal(state.settings.launchAnimMode, 'spinner');
  assert.equal(state.settings.returnAnimMode, 'liquid');

  // Row 3 (Game Return) -> instant; rows 0/1/2 untouched
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'LEFT');
  assert.equal(state.settings.appOpenAnim, 'zoom');
  assert.equal(state.settings.appCloseAnim, 'fade');
  assert.equal(state.settings.launchAnimMode, 'spinner');
  assert.equal(state.settings.returnAnimMode, 'instant');

  // Instant return emits zero returnDropSeq bump and restores library immediately
  state = handleLauncherKey(state, 'ESC');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'LIBRARY');
  const seqBefore = state.returnDropSeq ?? 0;
  state = handleLauncherKey(state, 'EXE');
  assert.notEqual(state.runningGameIndex, null);
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.runningGameIndex, null);
  assert.equal(state.returnDropSeq ?? 0, seqBefore);

  // Persistence across reload & safe factory reset
  const reloaded = scanVirtualFilesystem(
    files,
    '/documents/ndless/N-Launcher.tns',
    state.games,
    state.playCounter,
    {
      displayMode: state.displayMode,
      accentColor: state.accentColor,
      settings: state.settings,
      profiles: state.profiles,
    }
  );
  assert.equal(reloaded.settings.appOpenAnim, 'zoom');
  assert.equal(reloaded.settings.appCloseAnim, 'fade');
  assert.equal(reloaded.settings.launchAnimMode, 'spinner');
  assert.equal(reloaded.settings.returnAnimMode, 'instant');

  const afterReset = resetSettingsOnly(reloaded);
  assert.equal(afterReset.settings.appOpenAnim, 'current');
  assert.equal(afterReset.settings.appCloseAnim, 'liquid');
  assert.equal(afterReset.settings.launchAnimMode, 'current');
  assert.equal(afterReset.settings.returnAnimMode, 'liquid');
});





