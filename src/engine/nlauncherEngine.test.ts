import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {
  ACCENT_COLOR_ORDER,
  CATEGORY_LABELS,
  CATEGORY_ORDER,
  DISPLAY_MODE_ORDER,
  LAYOUT_ORDER,
  NL_COVER_RADIUS,
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
  getLayoutMetrics,
  getShortcutDefs,
  getTouchpadThresholdForLevel,
  handleCharInput,
  handleLauncherKey,
  inspectTnsBuffer,
  isShortcutAssignable,
  isShortcutReserved,
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
  CURSOR_STYLE_LABELS,
  CURSOR_STYLE_ORDER,
  computeTouchpadCursorTarget,
  isTouchpadCursorVisible,
  resetSettingsOnly,
  resetTouchpadTracker,
  scanVirtualFilesystem,
  setTouchpadControl,
  setUserProfileAvatar,
  stepProfile,
  switchProfileTo,
  toggleSimulatedNdlessPersistence,
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
  assert.equal(NL_ICON_MAX_DIM, 128);
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
  assert.equal(binInfo.allocSize > 500000, true);
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
        assert.equal(m.cardW >= 32, true);
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
  const def3x2 = getLayoutMetrics('3x2', false, true);
  assert.equal(def3x2.cardW, 84);
  assert.equal(def3x2.cardH, 84);
  assert.equal(def3x2.radius, 11);

  const exp3x2 = getLayoutMetrics('3x2', false, false);
  assert.equal(exp3x2.cardW, 90);
  assert.equal(exp3x2.cardH, 90);
  assert.equal(exp3x2.viewBottom, 222);
  assert.equal(exp3x2.paginationY, 226);
  const pagNoSc = computePaginationIndicators(0, 2, 0, exp3x2.paginationY);
  assert.equal(pagNoSc[0].y, 226);

  // Toggle showShortcuts in SETTINGS_DISPLAY (row 0 of SETTINGS_MAIN, option 2 of SETTINGS_DISPLAY)
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_DISPLAY');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_DISPLAY');
  assert.equal(state.sui.displayCursor, 0);
  state = handleLauncherKey(state, 'DOWN'); // row 1 (showNames)
  assert.equal(state.sui.displayCursor, 1);
  state = handleLauncherKey(state, 'DOWN'); // row 2 (showShortcuts)
  assert.equal(state.sui.displayCursor, 2);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.showShortcuts, false);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.showShortcuts, true);
  state = handleLauncherKey(state, 'ESC'); // back to SETTINGS_MAIN

  // Toggle Touchpad Navigation in SETTINGS_TOUCHPAD (row 1 of SETTINGS_MAIN)
  state = handleLauncherKey(state, 'DOWN'); // 1: Touchpad
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_TOUCHPAD');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.settings.touchpadNav, false);
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
  state = handleLauncherKey(state, 'DOWN'); // 1: Touchpad
  state = handleLauncherKey(state, 'DOWN'); // 2: Shortcuts
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
      layout: '4x4',
      showNames: true,
      showShortcuts: false,
      showBattery: false,
      touchpadNav: true,
      touchpadSensitivity: 4,
      touchpadCursorShow: true,
      touchpadCursorStyle: 'circle',
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
  assert.equal(state.settings.touchpadNav, false);
  assert.equal(state.settings.touchpadSensitivity, NL_TP_SENS_DEFAULT);
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

test('battery display removed cleanly & Settings -> Affichage exposes 4 streamlined options + DEL poweroff', () => {
  assert.equal(NL_DISPLAY_OPT_COUNT, 4);

  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.settings.showBattery, false);
  assert.equal(state.settings.autostart, false);

  // Settings -> Affichage (row 0 of SETTINGS_MAIN) has 4 options (0..3), ending with poweroff at row 3
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_DISPLAY');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_DISPLAY');
  assert.equal(state.sui.displayCursor, 0);

  state = handleLauncherKey(state, 'DOWN'); // 1: Afficher les noms
  state = handleLauncherKey(state, 'DOWN'); // 2: Afficher les raccourcis
  state = handleLauncherKey(state, 'DOWN'); // 3: Eteindre la calculatrice
  assert.equal(state.sui.displayCursor, 3);
  assert.equal(state.sui.displayScroll, 1);

  // Wrap around from row 3 -> 0 (no 5th battery option!)
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.displayCursor, 0);
  state = handleLauncherKey(state, 'UP');
  assert.equal(state.sui.displayCursor, 3);

  // Trigger power-off action on row 3
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

test('settings menu visual & ergonomic overhaul: 14 icon-badged categories including SETTINGS_PROFILES without SETTINGS_AUTOSTART or SETTINGS_SEARCH', () => {
  assert.equal(SETTINGS_MENU_ITEMS.length, 14);
  assert.equal(
    SETTINGS_MENU_ITEMS.some((i) => (i.id as string) === 'SETTINGS_SEARCH'),
    false
  );
  assert.equal(
    SETTINGS_MENU_ITEMS.some((i) => (i.id as string) === 'SETTINGS_AUTOSTART'),
    false
  );

  const refreshItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_REFRESH_LIB');
  const hotkeyItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_HOTKEY');
  const profilesItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_PROFILES');
  const shortcutsItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_SHORTCUTS');
  const keyHelpItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_KEY_HELP');
  const capBatchItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_CAPTURE_BATCH');
  const iconsItem = SETTINGS_MENU_ITEMS.find((i) => i.id === 'SETTINGS_ICONS');
  assert.equal(refreshItem?.label, 'Actualiser la bibliotheque');
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

  const iconSet = new Set(SETTINGS_MENU_ITEMS.map((i) => i.icon));
  assert.equal(iconSet.size, 14);

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

  // 2. Open Settings -> Generer captures d'ecran (index 6)
  state = handleLauncherKey(state, 'SETTINGS');
  for (let i = 0; i < 6; i++) {
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
  assert.equal(dec32!.width, 94);
  assert.equal(dec32!.height, 68);
  assert.equal(dec32!.bpp, 32);
  assert.equal(dec32!.rgb565Pixels.length, 94 * 68);
  assert.match(dec32!.dataUrl, /^data:image\/png;base64,/);

  // 2. Decode genuine 24-bit RGB Windows .ico (64x64 square -> zero crop, preserved 64x64)
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

  // 2b. Aspect-preserving downscale of non-square high-res 16:9 (160x90 -> 128x72) & 3:4 (120x160 -> 96x128)
  const ico16x9 = buildWindowsIcoBuffer({ width: 160, height: 90, bpp: 32, alpha: 255 });
  const dec16x9 = decodeWindowsIcoBuffer(ico16x9);
  assert.notEqual(dec16x9, null);
  assert.equal(dec16x9!.width, 128);
  assert.equal(dec16x9!.height, 72);

  const ico3x4 = buildWindowsIcoBuffer({ width: 120, height: 160, bpp: 32, alpha: 255 });
  const dec3x4 = decodeWindowsIcoBuffer(ico3x4);
  assert.notEqual(dec3x4, null);
  assert.equal(dec3x4!.width, 96);
  assert.equal(dec3x4!.height, 128);

  // 3. Decode genuine 16-bit XRGB1555 Windows .ico (94x68)
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
  assert.equal(dec16!.rgb565Pixels.length, 94 * 68);

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

  // SETTINGS_TOUCHPAD keypad navigation (row 0 = toggle ON/OFF, row 1 = 5-detent slider)
  let state = scanVirtualFilesystem(createInitialVirtualFilesystem());
  assert.equal(state.settings.touchpadSensitivity, 2);
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN'); // 1: Touchpad
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_TOUCHPAD');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.sui.tpCursor, 0);

  // Move to slider row (row 1) and step across detents 2 -> 3 -> 4 (clamped) -> 3 -> 0 (clamped)
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(state.sui.tpCursor, 1);
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
  state = setTouchpadControl(state, 1, 3, false);
  assert.equal(state.sui.tpCursor, 1);
  assert.equal(state.settings.touchpadSensitivity, 3);
  state = setTouchpadControl(state, 0, undefined, true);
  assert.equal(state.sui.tpCursor, 0);
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

  // Open Settings -> item 8 is Actualiser la bibliotheque (SETTINGS_REFRESH_LIB)
  state = { ...state, sys: { ...state.sys, cacheValid: false } };
  state = handleLauncherKey(state, 'SETTINGS');
  assert.equal(state.sui.screen, 'SETTINGS_MAIN');
  for (let i = 0; i < 8; i++) {
    state = handleLauncherKey(state, 'DOWN');
  }
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_REFRESH_LIB');
  state = handleLauncherKey(state, 'EXE');
  // Executing SETTINGS_REFRESH_LIB refreshes the library, rebuilds CRC32 cache & returns to LIBRARY
  assert.equal(state.sui.screen, 'LIBRARY');
  assert.equal(state.sys.cacheValid, true);
  assert.equal(state.toastMessage, 'BIBLIOTHEQUE ACTUALISEE');

  // Open Settings -> item 9 is Raccourci global TI-OS (SETTINGS_HOTKEY)
  state = handleLauncherKey(state, 'SETTINGS');
  for (let i = 0; i < 9; i++) {
    state = handleLauncherKey(state, 'DOWN');
  }
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_HOTKEY');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_HOTKEY');

  // Enable and disable globalHotkey in SETTINGS_HOTKEY
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.globalHotkey, true);
  assert.equal(state.sys.hotkeyActive, true);
  assert.equal(state.toastMessage, 'Raccourci TI-OS : ON');
  assert.equal(
    getShortcutDefs(state).some((d) => d.keyLabel === 'CTRL+MENU'),
    true
  );

  // Immediate disable on EXE
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.globalHotkey, false);
  assert.equal(state.sys.hotkeyActive, false);
  assert.equal(state.toastMessage, 'Raccourci TI-OS : OFF');

  // Re-enable and verify resetSettingsOnly cleanly disables globalHotkey and unhooks nl_hotkey.tns
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.globalHotkey, true);
  const resetState = resetSettingsOnly(state);
  assert.equal(resetState.settings.autostart, false);
  assert.equal(resetState.settings.globalHotkey, false);
  assert.equal(resetState.sys.hotkeyActive, false);
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

test('customizable touchpad cursor (Afficher le curseur OUI/NON, Personnaliser le curseur tab, Arrow/Circle/None styles, 5 grid layouts, no auto-launch, and persistence)', () => {
  const files = createDefaultSimulatorFilesystem();
  let state = scanVirtualFilesystem(files);

  // 1. Default state: cursorless navigation preserved by default (touchpadCursorShow = false, style = 'arrow')
  assert.equal(state.settings.touchpadCursorShow, false);
  assert.equal(state.settings.touchpadCursorStyle, 'arrow');
  assert.equal(isTouchpadCursorVisible(state), false);
  assert.deepEqual(CURSOR_STYLE_ORDER, ['none', 'arrow', 'circle']);
  assert.equal(CURSOR_STYLE_LABELS.none, 'Aucun');
  assert.equal(CURSOR_STYLE_LABELS.arrow, 'Fleche');
  assert.equal(CURSOR_STYLE_LABELS.circle, 'Cercle');

  // 2. Open Settings -> Navigation tactile (index 1)
  state = handleLauncherKey(state, 'SETTINGS');
  state = handleLauncherKey(state, 'DOWN');
  assert.equal(SETTINGS_MENU_ITEMS[state.sui.menuCursor].id, 'SETTINGS_TOUCHPAD');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.screen, 'SETTINGS_TOUCHPAD');
  assert.equal(state.sui.tpTab, 0);
  assert.equal(state.sui.tpCursor, 0);

  // 3. Row 0 toggles touchpadNav, Row 2 toggles Afficher le curseur (independent of touchpadNav!)
  state = handleLauncherKey(state, 'EXE'); // Enable touchpadNav
  assert.equal(state.settings.touchpadNav, true);
  assert.equal(state.settings.touchpadCursorShow, false);
  assert.equal(isTouchpadCursorVisible(state), false);

  state = handleLauncherKey(state, 'DOWN'); // Row 1: Sensibilite
  assert.equal(state.sui.tpCursor, 1);
  state = handleLauncherKey(state, 'DOWN'); // Row 2: Afficher le curseur
  assert.equal(state.sui.tpCursor, 2);
  state = handleLauncherKey(state, 'EXE'); // Toggle ON -> OUI
  assert.equal(state.settings.touchpadCursorShow, true);
  assert.equal(state.toastMessage, 'Afficher le curseur : OUI');
  assert.equal(isTouchpadCursorVisible(state), true);

  // Disabling touchpadNav does NOT reset touchpadCursorShow or touchpadCursorStyle, and vice versa
  state = handleLauncherKey(state, 'UP');
  state = handleLauncherKey(state, 'UP'); // Row 0
  state = handleLauncherKey(state, 'EXE'); // touchpadNav OFF
  assert.equal(state.settings.touchpadNav, false);
  assert.equal(state.settings.touchpadCursorShow, true);
  assert.equal(isTouchpadCursorVisible(state), true);
  state = handleLauncherKey(state, 'EXE'); // touchpadNav ON again
  assert.equal(state.settings.touchpadNav, true);

  // 4. Open "Personnaliser le curseur" tab via Row 3 EXE or TAB key
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'DOWN');
  state = handleLauncherKey(state, 'DOWN'); // Row 3: Personnaliser le curseur
  assert.equal(state.sui.tpCursor, 3);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.sui.tpTab, 1);
  assert.equal(state.sui.tpStyleCursor, 1); // 'arrow' is index 1

  // Select Curseur 2 — Cercle (RIGHT -> EXE)
  state = handleLauncherKey(state, 'RIGHT');
  assert.equal(state.sui.tpStyleCursor, 2);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadCursorStyle, 'circle');
  assert.equal(state.toastMessage, 'Curseur : Cercle');
  assert.equal(isTouchpadCursorVisible(state), true);

  // Select Aucun curseur (LEFT -> LEFT -> EXE): preserves touchpadCursorShow=true while hiding graphical sprite
  state = handleLauncherKey(state, 'LEFT');
  state = handleLauncherKey(state, 'LEFT');
  assert.equal(state.sui.tpStyleCursor, 0);
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadCursorStyle, 'none');
  assert.equal(state.toastMessage, 'Curseur : Aucun');
  assert.equal(state.settings.touchpadCursorShow, true);
  assert.equal(isTouchpadCursorVisible(state), false);

  // Switch back to Fleche (RIGHT -> EXE) and return to Tab 0 via TAB
  state = handleLauncherKey(state, 'RIGHT');
  state = handleLauncherKey(state, 'EXE');
  assert.equal(state.settings.touchpadCursorStyle, 'arrow');
  assert.equal(isTouchpadCursorVisible(state), true);
  state = handleLauncherKey(state, 'TAB');
  assert.equal(state.sui.tpTab, 0);

  // Return to LIBRARY
  state = handleLauncherKey(state, 'ESC');
  state = handleLauncherKey(state, 'ESC');
  assert.equal(state.sui.screen, 'LIBRARY');

  // 5. Synchronized cursor target coordinates across all 5 grid layouts (1x1, 2x2, 3x2, 3x3, 4x4) and 4 directions
  for (const layout of LAYOUT_ORDER) {
    state = {
      ...state,
      settings: { ...state.settings, layout },
      selectedPos: 0,
      currentPage: 0,
    };
    const t0 = computeTouchpadCursorTarget(state);
    assert.equal(t0.x >= 10 && t0.x <= NL_SCREEN_W - 10, true);
    assert.equal(t0.y >= 20 && t0.y <= NL_SCREEN_H - 10, true);

    if (layout !== '1x1') {
      const sRight = handleLauncherKey(state, 'RIGHT');
      const tRight = computeTouchpadCursorTarget(sRight);
      assert.equal(tRight.x > t0.x, true);

      const sDown = handleLauncherKey(sRight, 'DOWN');
      const tDown = computeTouchpadCursorTarget(sDown);
      assert.equal(tDown.y > tRight.y, true);
    }
  }

  // 6. Moving the cursor with touchpad gestures NEVER launches a game automatically
  state = {
    ...state,
    settings: { ...state.settings, layout: '3x2', touchpadNav: true, touchpadCursorShow: true },
    selectedPos: 0,
  };
  const tracker = createTouchpadTracker();
  processTouchpadSample(tracker, true, true, 500, 800, 2328, 1691, true, state.settings.touchpadSensitivity);
  const navKey = processTouchpadSample(tracker, true, true, 1400, 800, 2328, 1691, true, state.settings.touchpadSensitivity);
  assert.equal(navKey, 'RIGHT');
  state = handleLauncherKey(state, navKey!);
  assert.equal(state.selectedPos, 1);
  assert.equal(state.runningGameIndex, null);

  // 7. Persistence across rescan & safe reset verification
  const rescanned = scanVirtualFilesystem(files, '/documents/ndless/N-Launcher.tns', state.games, state.playCounter, {
    displayMode: state.displayMode,
    accentColor: state.accentColor,
    settings: state.settings,
  });
  assert.equal(rescanned.settings.touchpadCursorShow, true);
  assert.equal(rescanned.settings.touchpadCursorStyle, 'arrow');

  const tpScreenState = {
    ...rescanned,
    sui: { ...rescanned.sui, screen: 'SETTINGS_TOUCHPAD' as const },
  };
  const directCtrl = setTouchpadControl(tpScreenState, 2, 3, false, false, 'circle', 1);
  assert.equal(directCtrl.settings.touchpadCursorShow, false);
  assert.equal(directCtrl.settings.touchpadCursorStyle, 'circle');
  assert.equal(directCtrl.sui.tpTab, 1);
  assert.equal(directCtrl.sui.tpStyleCursor, 2);

  const afterReset = resetSettingsOnly(directCtrl);
  assert.equal(afterReset.settings.touchpadCursorShow, false);
  assert.equal(afterReset.settings.touchpadCursorStyle, 'arrow');
});

test('deliverable N-Launcher.tns synchronization, Build 2026-11-17 #R7 stamp, and SHA-256 verification', async () => {
  const {
    EXPECTED_TNS_BUILD_STAMP,
    EXPECTED_TNS_SHA256,
    EXPECTED_TNS_SIZE_BYTES,
    computeSha256Hex,
    verifyTnsBuffer,
  } = await import('../utils/downloadTns');

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
    assert.equal(check.buildStamp, EXPECTED_TNS_BUILD_STAMP);
    assert.equal(check.isLatestBuild, true);
    const sha = await computeSha256Hex(ab);
    assert.equal(sha, EXPECTED_TNS_SHA256);
  }
});

