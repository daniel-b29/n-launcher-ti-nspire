import React, { forwardRef, useEffect, useImperativeHandle, useRef } from 'react';
import {
  ASSIGNABLE_SHORTCUT_KEYS,
  CATEGORY_LABELS,
  CATEGORY_ORDER,
  CURSOR_SPEED_LABELS,
  CURSOR_STYLE_LABELS,
  CURSOR_STYLE_ORDER,
  CategoryId,
  ConsoleThemePalette,
  CursorStyle,
  GameEntry,
  LAYOUT_LABELS,
  LAYOUT_ORDER,
  LayoutMode,
  LauncherState,
  NL_AVATAR_CAT_COUNT,
  NL_AVATAR_COUNT,
  NL_BOTTOM_BAR_H,
  NL_BOTTOM_BAR_Y,
  NL_BUILD_STAMP_STR,
  NL_COVER_RADIUS,
  NL_CURSOR_SPEED_COUNT,
  NL_CURSOR_SPEED_DEFAULT,
  NL_DISPLAY_OPT_COUNT,
  NL_GEAR_BTN_D,
  NL_GEAR_BTN_X,
  NL_GEAR_BTN_Y,
  NL_ICON_H,
  NL_ICON_W,
  NL_LAUNCHER_VERSION_STR,
  NL_MAX_PROFILES,
  NL_PROFILE_NAME_MAX,
  NL_SCALE_NORMAL,
  NL_SCALE_SELECTED,
  NL_SCREEN_H,
  NL_SCREEN_W,
  NL_TOP_BAR_H,
  NL_TP_SENS_COUNT,
  NL_TP_SENS_DEFAULT,
  NL_VIEW_BOTTOM,
  NL_VIEW_TOP,
  NavKey,
  SETTINGS_MENU_ITEMS,
  SORT_ORDER_LABELS,
  SORT_ORDER_LIST,
  SettingsIconId,
  TOUCHPAD_MODE_LABELS,
  TP_SENS_LABELS,
  TouchpadMode,
  cardIndexToCursorStyle,
  clampCursorSpeed,
  clampCursorStyleToCard,
  clampTouchpadSensitivity,
  computeGameMonogram,
  computeGridSlotPos,
  computeNavHighlightStrength,
  computePaginationIndicators,
  computeTouchpadCursorTarget,
  findAvatarOwner,
  findShortcutOwner,
  firstFreeAvatar,
  formatDuration,
  formatTimestamp,
  getActiveProfile,
  getAvatarCategoryLabel,
  getAvatarCategoryOf,
  getAvatarCategoryRange,
  getAvatarImageUrl,
  getConsoleTheme,
  getLayoutMetrics,
  getShortcutDefs,
  isMouseModeActive,
  isTouchpadCursorVisible,
} from '../engine/nlauncherEngine';

type SettingsScreenId = LauncherState['sui']['screen'];

function lerpHexColor(hexA: string, hexB: string, t: number): string {
  const clamped = Math.max(0, Math.min(1, t));
  if (clamped <= 0) return hexA;
  if (clamped >= 1) return hexB;
  const a = parseInt(hexA.slice(1), 16);
  const b = parseInt(hexB.slice(1), 16);
  const ar = (a >> 16) & 255;
  const ag = (a >> 8) & 255;
  const ab = a & 255;
  const br = (b >> 16) & 255;
  const bg = (b >> 8) & 255;
  const bb = b & 255;
  const r = Math.round(ar + (br - ar) * clamped);
  const g = Math.round(ag + (bg - ag) * clamped);
  const bl = Math.round(ab + (bb - ab) * clamped);
  return `rgb(${r}, ${g}, ${bl})`;
}

/* Exact JS mirror of C hardware pure-integer organic ink field (4-quadrant LUT + monotonic connected front) */
const NL_INK_SIN256 = (() => {
  const lut = new Int8Array(256);
  for (let i = 0; i <= 64; i++) {
    const p = i * 4;
    const x = i;
    const num = 4 * x * (128 - x);
    const den = 20480 - p * (128 - x);
    let v = Math.floor((127 * num + (den >> 1)) / den);
    if (v > 127) v = 127;
    lut[i] = v;
    lut[128 - i] = v;
  }
  for (let i = 0; i < 128; i++) {
    lut[128 + i] = -lut[i];
  }
  return lut;
})();

function inkIsqrt32Js(val: number): number {
  if (val <= 0) return 0;
  return Math.floor(Math.sqrt(val));
}

function inkOctant64Js(ux: number, uy: number): number {
  if (ux <= 0 && uy <= 0) return 0;
  const big = ux >= uy ? ux : uy;
  const sml = ux >= uy ? uy : ux;
  const ratio = Math.floor((sml * 64) / big);
  const oct = (ratio * 32 + Math.floor(((64 - ratio) * ratio * 9) / 64)) >> 6;
  return ux >= uy ? oct : 64 - oct;
}

function buildOrganicInkFieldJs(seed: number): { field: Uint8Array; maxVal: number } {
  const s0 = (seed * 37 + 13) & 255;
  const s1 = (seed * 73 + 41) & 255;
  const s2 = (seed * 113 + 97) & 255;
  const s3 = (seed * 157 + 19) & 255;

  const dirScale = new Int32Array(256);
  for (let th = 0; th < 256; th++) {
    const m1 = NL_INK_SIN256[(th + s0) & 255];
    const m2 = NL_INK_SIN256[(th * 2 + s1) & 255];
    const m3 = NL_INK_SIN256[(th * 3 + s2) & 255];
    const m5 = NL_INK_SIN256[(th * 5 + s3) & 255];
    const bias = (m1 * 10 + m2 * 13 + m3 * 12 + m5 * 7) >> 7;
    dirScale[th] = 256 + bias;
  }

  const wx = new Int8Array(NL_SCREEN_W);
  const wy = new Int8Array(NL_SCREEN_H);
  for (let x = 0; x < NL_SCREEN_W; x++) {
    wx[x] = (NL_INK_SIN256[(x * 2 + s1) & 255] * 4) >> 7;
  }
  for (let y = 0; y < NL_SCREEN_H; y++) {
    wy[y] = (NL_INK_SIN256[(y * 2 + 64 + s2) & 255] * 4) >> 7;
  }

  const field = new Uint8Array(NL_SCREEN_W * NL_SCREEN_H);
  let maxVal = 1;

  for (let uy = 0; uy < 120; uy++) {
    const yLo = 120 + uy;
    const yHi = 119 - uy;
    const rowLoOff = yLo * NL_SCREEN_W;
    const rowHiOff = yHi * NL_SCREEN_W;
    const uy2 = uy * uy;

    for (let ux = 0; ux < 160; ux++) {
      const r = inkIsqrt32Js(ux * ux + uy2);
      const a0 = inkOctant64Js(ux, uy);
      const ramp = r < 56 ? Math.floor((r * 256) / 56) : 256;
      const xR = 160 + ux;
      const xL = 159 - ux;

      // Q0: bottom-right
      {
        const th = a0 & 255;
        const wd = (NL_INK_SIN256[((xR + yLo) * 2 + s3) & 255] * 3) >> 7;
        let d = ((r * dirScale[th]) >> 8) + ((ramp * (wx[xR] + wy[yLo] + wd)) >> 8);
        if (d < 0) d = 0;
        else if (d > 240) d = 240;
        field[rowLoOff + xR] = d;
        if (d > maxVal) maxVal = d;
      }
      // Q1: bottom-left
      {
        const th = (128 - a0) & 255;
        const wd = (NL_INK_SIN256[((xL + yLo) * 2 + s3) & 255] * 3) >> 7;
        let d = ((r * dirScale[th]) >> 8) + ((ramp * (wx[xL] + wy[yLo] + wd)) >> 8);
        if (d < 0) d = 0;
        else if (d > 240) d = 240;
        field[rowLoOff + xL] = d;
        if (d > maxVal) maxVal = d;
      }
      // Q2: top-left
      {
        const th = (128 + a0) & 255;
        const wd = (NL_INK_SIN256[((xL + yHi) * 2 + s3) & 255] * 3) >> 7;
        let d = ((r * dirScale[th]) >> 8) + ((ramp * (wx[xL] + wy[yHi] + wd)) >> 8);
        if (d < 0) d = 0;
        else if (d > 240) d = 240;
        field[rowHiOff + xL] = d;
        if (d > maxVal) maxVal = d;
      }
      // Q3: top-right
      {
        const th = (256 - a0) & 255;
        const wd = (NL_INK_SIN256[((xR + yHi) * 2 + s3) & 255] * 3) >> 7;
        let d = ((r * dirScale[th]) >> 8) + ((ramp * (wx[xR] + wy[yHi] + wd)) >> 8);
        if (d < 0) d = 0;
        else if (d > 240) d = 240;
        field[rowHiOff + xR] = d;
        if (d > maxVal) maxVal = d;
      }
    }
  }

  return { field, maxVal };
}

interface NspireScreenCanvasProps {
  state: LauncherState;
  scale: 1 | 2;
  bootSeq?: number;
  onKeyCommand: (key: NavKey) => void;
  onSelectSlot: (filteredPos: number) => void;
  onCaptureDuringGame: (gameIndex: number, capturedDataUrl: string) => void;
  onMoveMouse?: (x: number, y: number) => void;
  onClickMouse?: () => void;
  onTouchpadControl?: (
    tpCursor: 0 | 1 | 2 | 3 | 4,
    sensitivity?: number,
    toggleNav?: boolean,
    cursorSpeed?: number,
    cursorStyle?: CursorStyle,
    tpTab?: 0 | 1,
    toggleMode?: boolean,
    explicitMode?: TouchpadMode
  ) => void;
}

function traceRoundedRect(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number
) {
  const rad = Math.max(0, Math.min(r, Math.min(w, h) / 2));
  ctx.beginPath();
  ctx.moveTo(x + rad, y);
  ctx.lineTo(x + w - rad, y);
  ctx.quadraticCurveTo(x + w, y, x + w, y + rad);
  ctx.lineTo(x + w, y + h - rad);
  ctx.quadraticCurveTo(x + w, y + h, x + w - rad, y + h);
  ctx.lineTo(x + rad, y + h);
  ctx.quadraticCurveTo(x, y + h, x, y + h - rad);
  ctx.lineTo(x, y + rad);
  ctx.quadraticCurveTo(x, y, x + rad, y);
  ctx.closePath();
}

function traceCapsule(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number
) {
  const r = h / 2;
  if (w <= h) {
    ctx.beginPath();
    ctx.arc(x + w / 2, y + r, r, 0, Math.PI * 2);
    ctx.closePath();
    return;
  }
  ctx.beginPath();
  ctx.arc(x + r, y + r, r, Math.PI * 0.5, Math.PI * 1.5);
  ctx.lineTo(x + w - r, y);
  ctx.arc(x + w - r, y + r, r, -Math.PI * 0.5, Math.PI * 0.5);
  ctx.closePath();
}

function drawLauncherEmblem(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  size: number,
  theme: ConsoleThemePalette
) {
  const rad = Math.max(3, Math.round((size * 26) / 100));
  traceRoundedRect(ctx, x, y, size, size, rad);
  ctx.fillStyle = theme.accentPrimary;
  ctx.fill();

  const pad = Math.max(3, Math.round(size * 0.25));
  const innerH = size - pad * 2;
  const strokeW = Math.max(2, Math.round((size * 13 + 50) / 100));

  ctx.strokeStyle = theme.textOnAccent;
  ctx.lineWidth = strokeW;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.beginPath();
  ctx.moveTo(x + pad, y + pad + innerH);
  ctx.lineTo(x + pad, y + pad);
  ctx.lineTo(x + size - pad, y + pad + innerH);
  ctx.lineTo(x + size - pad, y + pad);
  ctx.stroke();
}

const NL_SETTINGS_BLOB_DATA = (() => {
  const cx = NL_GEAR_BTN_X + NL_GEAR_BTN_D / 2; // 302.5 -> 302
  const cy = NL_GEAR_BTN_Y + NL_GEAR_BTN_D / 2; // 12.5 -> 12
  const dirScale = new Int32Array(256);
  for (let th = 0; th < 256; th++) {
    const m1 = NL_INK_SIN256[(th + 29) & 255];
    const m2 = NL_INK_SIN256[(th * 2 + 87) & 255];
    const m3 = NL_INK_SIN256[(th * 3 + 149) & 255];
    const m5 = NL_INK_SIN256[(th * 5 + 43) & 255];
    const bias = (m1 * 18 + m2 * 22 + m3 * 16 + m5 * 9) >> 7;
    dirScale[th] = 256 + bias;
  }

  const field = new Uint16Array(NL_SCREEN_W * NL_SCREEN_H);
  let maxVal = 1;
  for (let py = 0; py < NL_SCREEN_H; py++) {
    const dy = py - Math.floor(cy);
    const dy2 = dy * dy;
    const rowOff = py * NL_SCREEN_W;
    for (let px = 0; px < NL_SCREEN_W; px++) {
      const dx = px - Math.floor(cx);
      const r = inkIsqrt32Js(dx * dx + dy2);
      let angle = Math.atan2(dy, dx);
      if (angle < 0) angle += Math.PI * 2;
      const th256 = Math.floor((angle * 256) / (Math.PI * 2)) & 255;
      const wave =
        (NL_INK_SIN256[(px * 3 + py * 2 + 17) & 255] * 4 +
          NL_INK_SIN256[(px * 2 - py * 4 + 91) & 255] * 3) >>
        7;
      const ramp = r < 36 ? Math.floor((r * 256) / 36) : 256;
      let d = ((r * dirScale[th256]) >> 8) + ((ramp * wave) >> 8);
      if (d < 0) d = 0;
      field[rowOff + px] = d;
      if (d > maxVal) maxVal = d;
    }
  }
  return { field, maxVal };
})();

function hexToRgbTuple(hex: string): [number, number, number] {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function drawGearButton(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  d: number,
  theme: ConsoleThemePalette,
  active: boolean,
  rotationRad = 0
) {
  const cx = x + d / 2;
  const cy = y + d / 2;
  const r = d / 2;

  ctx.save();
  ctx.translate(cx, cy);
  if (rotationRad !== 0) {
    ctx.rotate(rotationRad);
  }

  ctx.beginPath();
  ctx.arc(0, 0, r, 0, Math.PI * 2);
  ctx.fillStyle = active ? theme.accentSecondary : theme.accentPrimary;
  ctx.fill();

  const fg = theme.textOnAccent;
  ctx.fillStyle = fg;
  const teethR = r - 2.3;
  for (let i = 0; i < 8; i++) {
    const ang = (i * Math.PI) / 4;
    const tx = Math.cos(ang) * teethR;
    const ty = Math.sin(ang) * teethR;
    ctx.beginPath();
    ctx.arc(tx, ty, 1.15, 0, Math.PI * 2);
    ctx.fill();
  }

  ctx.beginPath();
  ctx.arc(0, 0, r - 3.3, 0, Math.PI * 2);
  ctx.fillStyle = fg;
  ctx.fill();

  ctx.beginPath();
  ctx.arc(0, 0, 1.85, 0, Math.PI * 2);
  ctx.fillStyle = active ? theme.accentSecondary : theme.accentPrimary;
  ctx.fill();
  ctx.restore();
}

function drawToggleSwitch(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  animPos: number,
  theme: ConsoleThemePalette
) {
  const w = 30;
  const h = 16;
  const pos = Math.max(0, Math.min(1, animPos));
  const eased = pos < 0.5 ? 4 * pos * pos * pos : 1 - Math.pow(-2 * pos + 2, 3) / 2;
  const stretch = Math.sin(pos * Math.PI) * 2;
  const trackColor = lerpHexColor(theme.dotInactive, theme.accentPrimary, eased);
  traceCapsule(ctx, x, y, w, h);
  ctx.fillStyle = trackColor;
  ctx.fill();

  const knobH = 12;
  const knobW = 12 + stretch;
  const knobX = x + 2 + eased * (w - knobW - 4);
  const knobY = y + 2;
  traceCapsule(ctx, knobX, knobY, knobW, knobH);
  ctx.fillStyle = '#ffffff';
  ctx.fill();
}

/**
 * Draw a single rounded square icon card with STRICT clipping & aspect preservation:
 * - Zero cropping or stretching of custom icons ("contain" placement over theme cardBg)
 * - Smooth anti-aliased rounded mask + discreet 1px outer border
 * - Real game covers ALWAYS keep their exact original colors
 * - Selection conveyed by smooth scale + subtle shadow + ultra-faint neutral luminosity
 */
function drawGameCoverCard(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  radius: number,
  game: GameEntry,
  hasShadow: boolean,
  highlightStrength: number,
  theme: ConsoleThemePalette,
  imgCache: Map<string, HTMLImageElement>
) {
  const hasRealCover = game.iconSource !== 'NONE' && Boolean(game.capturedIconDataUrl);
  const cached = hasRealCover && game.capturedIconDataUrl
    ? imgCache.get(game.capturedIconDataUrl)
    : undefined;

  let bx = x;
  let by = y;
  let bw = w;
  let bh = h;
  if (cached && cached.complete && cached.naturalWidth > 0) {
    const srcW = cached.naturalWidth;
    const srcH = cached.naturalHeight;
    const srcRatio = srcW / srcH;
    const dstRatio = w / h;
    if (srcRatio > dstRatio) {
      bh = w / srcRatio;
    } else if (srcRatio < dstRatio) {
      bw = h * srcRatio;
    }
    bx = x + (w - bw) / 2;
    by = y + (h - bh) / 2;
  }

  if (highlightStrength > 0.01) {
    ctx.save();
    traceRoundedRect(ctx, bx - 2, by - 2, bw + 4, bh + 4, radius + 2);
    ctx.fillStyle = theme.isLight
      ? `rgba(148, 163, 184, ${(0.075 * highlightStrength).toFixed(4)})`
      : `rgba(235, 242, 252, ${(0.032 * highlightStrength).toFixed(4)})`;
    ctx.fill();
    traceRoundedRect(ctx, bx - 1, by - 1, bw + 2, bh + 2, radius + 1);
    ctx.fillStyle = theme.isLight
      ? `rgba(148, 163, 184, ${(0.11 * highlightStrength).toFixed(4)})`
      : `rgba(235, 242, 252, ${(0.052 * highlightStrength).toFixed(4)})`;
    ctx.fill();
    ctx.restore();
  }

  if (hasShadow) {
    ctx.save();
    traceRoundedRect(ctx, bx + 2, by + 3, bw, bh, radius);
    ctx.fillStyle = theme.isLight ? 'rgba(15, 23, 42, 0.16)' : 'rgba(0, 0, 0, 0.48)';
    ctx.fill();
    ctx.restore();
  }

  ctx.save();
  traceRoundedRect(ctx, bx, by, bw, bh, radius);
  ctx.clip();

  if (cached && cached.complete && cached.naturalWidth > 0) {
    // Harmonized subtle neutral backdrop for transparent .ico regions (matches C renderer 5:3 blend)
    ctx.fillStyle = lerpHexColor(theme.bgMain, theme.cardBg, 0.625);
    ctx.fillRect(bx, by, bw, bh);

    const srcW = cached.naturalWidth;
    const srcH = cached.naturalHeight;

    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    if (srcW > bw * 1.5 || srcH > bh * 1.5) {
      // Progressive 2x downsampling steps prevent aliasing/blur on small 3x3 and 4x4 grid cards
      const targetPxW = Math.max(1, Math.round(bw * 2));
      const targetPxH = Math.max(1, Math.round(bh * 2));
      let curW = srcW;
      let curH = srcH;
      const stepC = document.createElement('canvas');
      const stepCtx = stepC.getContext('2d');
      if (stepCtx) {
        stepC.width = curW;
        stepC.height = curH;
        stepCtx.imageSmoothingEnabled = true;
        stepCtx.imageSmoothingQuality = 'high';
        stepCtx.drawImage(cached, 0, 0);
        while (curW * 0.5 >= targetPxW && curH * 0.5 >= targetPxH) {
          const nw = Math.max(targetPxW, Math.round(curW * 0.5));
          const nh = Math.max(targetPxH, Math.round(curH * 0.5));
          stepCtx.drawImage(stepC, 0, 0, curW, curH, 0, 0, nw, nh);
          curW = nw;
          curH = nh;
        }
        ctx.drawImage(stepC, 0, 0, curW, curH, bx, by, bw, bh);
      } else {
        ctx.drawImage(cached, 0, 0, srcW, srcH, bx, by, bw, bh);
      }
    } else {
      ctx.drawImage(cached, 0, 0, srcW, srcH, bx, by, bw, bh);
    }
    ctx.imageSmoothingEnabled = false;
  } else {
    // Minimal clean fallback cover harmonized with light/dark mode
    const grad = ctx.createLinearGradient(x, y, x, y + h);
    grad.addColorStop(0, theme.cardBg);
    grad.addColorStop(1, theme.isLight ? theme.bgTopBar : theme.bgMain);
    ctx.fillStyle = grad;
    ctx.fillRect(x, y, w, h);

    // Subtle colored wash from game accent
    ctx.fillStyle = game.accentHex;
    ctx.globalAlpha = theme.isLight ? 0.1 : 0.14;
    ctx.fillRect(x, y, w, h);
    ctx.globalAlpha = 1;

    const monoAccent = theme.isLight
      ? lerpHexColor(game.accentHex, theme.textPrimary, 0.25)
      : game.accentHex;

    // Minimal 2-char monogram scaled proportionally to card size
    const mono = computeGameMonogram(game.title);
    const largeMono = w >= 56 && h >= 42;
    ctx.font = largeMono
      ? 'bold 17px "JetBrains Mono", monospace'
      : w >= 38
      ? 'bold 11px "JetBrains Mono", monospace'
      : 'bold 9.5px "JetBrains Mono", monospace';
    ctx.textBaseline = 'middle';
    const c1 = mono[0] || 'G';
    const c2 = mono[1] || '';
    const charW = largeMono ? 10.5 : w >= 38 ? 7 : 6;
    const totalW = mono.length * charW;
    const tx = x + (w - totalW) / 2;
    const ty = y + h / 2 - 2;

    ctx.fillStyle = theme.textPrimary;
    ctx.fillText(c1, tx, ty);
    if (c2) {
      ctx.fillStyle = monoAccent;
      ctx.fillText(c2, tx + charW, ty);
    }

    // Subtle bottom accent strip inside rounded clip
    const barPad = w >= 70 ? 12 : w >= 46 ? 8 : 5;
    if (w - barPad * 2 >= 8) {
      ctx.fillStyle = monoAccent;
      ctx.globalAlpha = 0.72;
      traceRoundedRect(ctx, x + barPad, y + h - 7, w - barPad * 2, 3, 1.5);
      ctx.fill();
      ctx.globalAlpha = 1;
    }
  }

  if (highlightStrength > 0.01) {
    ctx.fillStyle = `rgba(255, 255, 255, ${(0.055 * highlightStrength).toFixed(4)})`;
    ctx.fillRect(bx, by, bw, bh);
  }

  // Tiny discreet favorite star badge inside top-right corner (when card size allows)
  if (game.isFavorite && w >= 42 && h >= 32) {
    traceRoundedRect(ctx, x + w - 15, y + 3, 12, 12, 3.5);
    ctx.fillStyle = 'rgba(14, 18, 26, 0.85)';
    ctx.fill();
    ctx.fillStyle = theme.badgeFav;
    ctx.font = 'bold 9px sans-serif';
    ctx.textBaseline = 'middle';
    ctx.fillText('★', x + w - 12.5, y + 9.5);
  }

  ctx.restore();

  // Discreet 1px outer perimeter border preserving AA rounded edge
  ctx.save();
  traceRoundedRect(ctx, bx + 0.5, by + 0.5, Math.max(1, bw - 1), Math.max(1, bh - 1), Math.max(1, radius - 0.5));
  ctx.strokeStyle = theme.borderSubtle;
  ctx.globalAlpha = 0.32;
  ctx.lineWidth = 1;
  ctx.stroke();
  ctx.restore();
}

/**
 * Live 320x240 child game VRAM framebuffer (used both on screen & for clean real capture)
 */
function renderChildGameVramScene(
  ctx: CanvasRenderingContext2D,
  game: GameEntry,
  timeMs: number,
  includeHudHint: boolean
) {
  const t = timeMs * 0.001;
  ctx.textBaseline = 'alphabetic';

  switch (game.sceneKind) {
    case 'DOOM': {
      ctx.fillStyle = '#240b0b';
      ctx.fillRect(0, 0, NL_SCREEN_W, 102);
      ctx.fillStyle = '#1b1917';
      ctx.fillRect(0, 102, NL_SCREEN_W, 98);

      const bob = Math.sin(t * 4.5) * 3;
      const wallColors = ['#5c1916', '#7f1d1d', '#991b1b', '#b91c1c'];
      for (let i = 0; i < 4; i++) {
        const insetX = i * 28;
        const insetY = i * 16 + bob * 0.4;
        ctx.fillStyle = wallColors[i];
        ctx.fillRect(insetX, insetY, 28, 200 - insetY * 2);
        ctx.fillRect(NL_SCREEN_W - insetX - 28, insetY, 28, 200 - insetY * 2);
      }

      const glow = 0.7 + 0.3 * Math.sin(t * 6);
      ctx.fillStyle = glow > 0.82 ? '#f97316' : '#dc2626';
      ctx.fillRect(116, 62 + bob * 0.5, 88, 78);
      ctx.fillStyle = '#fef08a';
      ctx.fillRect(144, 80 + bob * 0.5, 10, 12);
      ctx.fillRect(166, 80 + bob * 0.5, 10, 12);

      ctx.fillStyle = '#475569';
      ctx.fillRect(146, 152 + bob, 28, 48);
      ctx.fillStyle = '#94a3b8';
      ctx.fillRect(152, 146 + bob, 16, 16);

      ctx.fillStyle = '#292524';
      ctx.fillRect(0, 200, NL_SCREEN_W, 40);
      ctx.strokeStyle = '#57534e';
      ctx.lineWidth = 1;
      ctx.strokeRect(1, 201, NL_SCREEN_W - 2, 38);
      ctx.fillStyle = '#ef4444';
      ctx.font = 'bold 12px "JetBrains Mono", monospace';
      ctx.fillText('AMMO 48', 12, 224);
      ctx.fillText('100%', 118, 224);
      ctx.fillStyle = '#facc15';
      ctx.fillText('ARMOR 100', 216, 224);
      break;
    }

    case 'QUAKE': {
      ctx.fillStyle = '#1c140d';
      ctx.fillRect(0, 0, NL_SCREEN_W, 115);
      ctx.fillStyle = '#14100c';
      ctx.fillRect(0, 115, NL_SCREEN_W, 125);

      const pulse = Math.sin(t * 3.5) * 4;
      for (let i = 0; i < 5; i++) {
        ctx.strokeStyle = i % 2 === 0 ? '#78350f' : '#92400e';
        ctx.lineWidth = 3;
        ctx.strokeRect(20 + i * 22, 14 + i * 13, 280 - i * 44, 180 - i * 26);
      }
      ctx.fillStyle = '#f59e0b';
      ctx.fillRect(136, 72 + pulse * 0.4, 48, 64);
      ctx.fillStyle = '#292524';
      ctx.fillRect(0, 204, NL_SCREEN_W, 36);
      ctx.fillStyle = '#fbbf24';
      ctx.font = 'bold 11px "JetBrains Mono", monospace';
      ctx.fillText('QUAKE ARM · 100 HP · NAILS 85', 22, 226);
      break;
    }

    case 'CRAFTI': {
      ctx.fillStyle = '#38bdf8';
      ctx.fillRect(0, 0, NL_SCREEN_W, 118);
      const cloudX = ((t * 18) % 360) - 40;
      ctx.fillStyle = '#f8fafc';
      ctx.fillRect(cloudX, 22, 54, 14);
      ctx.fillRect(((cloudX + 170) % 360) - 20, 38, 68, 12);
      ctx.fillStyle = '#fde047';
      ctx.fillRect(256, 18, 28, 28);

      for (let col = 0; col < 10; col++) {
        const bx = col * 32;
        const hOff = ((col * 17) % 3) * 14;
        ctx.fillStyle = '#22c55e';
        ctx.fillRect(bx, 108 + hOff, 32, 16);
        ctx.fillStyle = '#854d0e';
        ctx.fillRect(bx, 124 + hOff, 32, 116 - hOff);
      }
      ctx.fillStyle = '#713f12';
      ctx.fillRect(76, 74, 14, 38);
      ctx.fillStyle = '#15803d';
      ctx.fillRect(58, 46, 50, 32);

      ctx.fillStyle = '#ffffff';
      ctx.fillRect(156, 109, 8, 2);
      ctx.fillRect(159, 106, 2, 8);

      ctx.fillStyle = 'rgba(15, 23, 42, 0.85)';
      ctx.fillRect(64, 210, 192, 24);
      ctx.strokeStyle = '#e2e8f0';
      ctx.strokeRect(64, 210, 24, 24);
      for (let s = 0; s < 8; s++) {
        ctx.fillStyle = s % 2 === 0 ? '#22c55e' : '#a16207';
        ctx.fillRect(68 + s * 24, 214, 16, 16);
      }
      break;
    }

    case 'SHMUP': {
      ctx.fillStyle = '#050814';
      ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);
      for (let i = 0; i < 28; i++) {
        const sx = (i * 37 + 13) % NL_SCREEN_W;
        const sy = Math.floor((i * 29 + t * (40 + (i % 3) * 28)) % NL_SCREEN_H);
        ctx.fillStyle = i % 3 === 0 ? '#38bdf8' : '#e2e8f0';
        ctx.fillRect(sx, sy, 2, 2);
      }

      const shipX = Math.floor(160 + Math.sin(t * 2.6) * 75);
      ctx.fillStyle = '#38bdf8';
      ctx.fillRect(shipX - 6, 40, 3, 145);
      ctx.fillStyle = '#f43f5e';
      ctx.fillRect(shipX + 4, 40, 3, 145);

      ctx.fillStyle = '#f8fafc';
      ctx.fillRect(shipX - 3, 184, 7, 22);
      ctx.fillStyle = '#0ea5e9';
      ctx.fillRect(shipX - 14, 194, 29, 7);

      for (let e = 0; e < 5; e++) {
        const ex = 48 + e * 52 + Math.sin(t * 3 + e) * 10;
        ctx.fillStyle = e % 2 === 0 ? '#f43f5e' : '#38bdf8';
        ctx.fillRect(ex, 42, 22, 16);
        ctx.fillStyle = '#fef08a';
        ctx.fillRect(ex + 7, 46, 8, 8);
      }
      break;
    }

    case '2048': {
      ctx.fillStyle = '#1c1917';
      ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);
      ctx.fillStyle = '#292524';
      ctx.fillRect(68, 24, 184, 184);

      const vals = [2, 4, 16, 64, 8, 128, 512, 32, 4, 32, 2048, 16, 2, 8, 64, 4];
      const colors: Record<number, string> = {
        2: '#44403c',
        4: '#57534e',
        8: '#ea580c',
        16: '#f97316',
        32: '#ef4444',
        64: '#dc2626',
        128: '#eab308',
        512: '#facc15',
        2048: '#10b981',
      };
      for (let idx = 0; idx < 16; idx++) {
        const c = idx % 4;
        const r = Math.floor(idx / 4);
        const tx = 74 + c * 44;
        const ty = 30 + r * 44;
        const v = vals[idx];
        ctx.fillStyle = colors[v] || '#f59e0b';
        ctx.fillRect(tx, ty, 40, 40);
        ctx.fillStyle = '#ffffff';
        ctx.font = 'bold 10px "JetBrains Mono", monospace';
        const s = String(v);
        ctx.fillText(s, tx + Math.floor((40 - s.length * 6) / 2), ty + 24);
      }
      break;
    }

    case 'GBC':
    case 'GBA':
    case 'NES': {
      const bezel =
        game.sceneKind === 'GBC'
          ? '#3b0764'
          : game.sceneKind === 'GBA'
          ? '#172554'
          : '#450a0a';
      ctx.fillStyle = bezel;
      ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);
      ctx.fillStyle = '#84cc16';
      ctx.fillRect(48, 20, 224, 184);
      ctx.fillStyle = '#65a30d';
      ctx.fillRect(48, 156, 224, 48);

      const px = 80 + Math.floor((t * 60) % 150);
      const py = 134 - Math.floor(Math.abs(Math.sin(t * 5.5)) * 32);
      ctx.fillStyle = '#1a2e05';
      ctx.fillRect(px, py, 16, 20);
      ctx.fillRect(120, 96, 20, 20);
      ctx.fillRect(164, 96, 20, 20);
      ctx.font = 'bold 10px "JetBrains Mono", monospace';
      ctx.fillText(`${game.sceneKind} 60 FPS`, 58, 38);
      break;
    }

    default: {
      ctx.fillStyle = '#060913';
      ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);
      ctx.strokeStyle = '#1d4ed8';
      ctx.lineWidth = 3;
      ctx.strokeRect(16, 18, 288, 196);
      ctx.strokeRect(56, 54, 84, 46);
      ctx.strokeRect(180, 54, 84, 46);
      ctx.strokeRect(56, 132, 208, 44);

      const pacX = 34 + Math.floor((t * 72) % 240);
      for (let d = 0; d < 11; d++) {
        ctx.fillStyle = '#fde68a';
        ctx.fillRect(36 + d * 24, 116, 4, 4);
      }
      ctx.fillStyle = '#facc15';
      ctx.fillRect(pacX, 108, 18, 18);
      ctx.fillStyle = '#ef4444';
      ctx.fillRect(216, 108, 18, 18);
      ctx.fillStyle = '#38bdf8';
      ctx.fillRect(244, 108, 18, 18);
      break;
    }
  }

  if (includeHudHint) {
    ctx.fillStyle = 'rgba(9, 13, 22, 0.88)';
    ctx.fillRect(0, 0, NL_SCREEN_W, 20);
    ctx.fillStyle = '#10b981';
    ctx.fillRect(0, 19, NL_SCREEN_W, 1);
    ctx.font = 'bold 8px "JetBrains Mono", monospace';
    ctx.fillText(
      `EN JEU : ${game.title.slice(0, 16)} · [ESC] Retour Launcher`,
      6,
      13
    );
  }
}

function isCapturedFrameValid(ctx: CanvasRenderingContext2D, w: number, h: number): boolean {
  const data = ctx.getImageData(0, 0, w, h).data;
  let totalSamples = 0;
  let nonBlack = 0;
  let transitions = 0;
  const hist64 = new Uint16Array(64);
  const bandEdges = new Uint16Array(8);
  const prevRowKeys = new Int32Array(80).fill(-1);

  for (let y = 8; y < h - 8; y += 4) {
    const band = Math.min(7, Math.max(0, Math.floor(((y - 8) * 8) / (h - 16))));
    let leftKey = -1;
    let col = 0;
    for (let x = 8; x < w - 8; x += 4) {
      const idx = (y * w + x) * 4;
      const r = data[idx];
      const g = data[idx + 1];
      const b = data[idx + 2];
      totalSamples++;

      const r5 = r >> 3;
      const g6 = g >> 2;
      const b5 = b >> 3;
      const luma = r5 * 2 + g6 + b5 * 2;
      if (luma > 10) {
        nonBlack++;
      }

      const key = (r5 << 11) | (g6 << 5) | b5;
      if (x > 8 && key !== leftKey) {
        transitions++;
        bandEdges[band]++;
      }
      if (y > 8 && col < 80 && key !== prevRowKeys[col]) {
        transitions++;
        bandEdges[band]++;
      }
      leftKey = key;
      if (col < 80) prevRowKeys[col] = key;
      col++;

      const bin = ((r5 >> 3) << 4) | ((g6 >> 4) << 2) | (b5 >> 3);
      hist64[bin]++;
    }
  }

  if (totalSamples <= 0 || nonBlack < 50) return false;
  let maxBinCount = 0;
  let distinctBins = 0;
  for (let i = 0; i < 64; i++) {
    if (hist64[i] > maxBinCount) maxBinCount = hist64[i];
    if (hist64[i] >= 2) distinctBins++;
  }
  const nonDomPixels = totalSamples - maxBinCount;
  if (nonDomPixels < 50 || distinctBins < 2) return false;

  let activeBands = 0;
  for (let i = 0; i < 8; i++) {
    if (bandEdges[i] >= 2) activeBands++;
  }
  return transitions >= 24 && activeBands >= 3;
}

const imgCacheRef_global = new Map<string, HTMLImageElement>();

export function captureRealGameScreenshotDataUrl(
  target: GameEntry,
  timeMs: number
): string | null {
  const fullOffscreen = document.createElement('canvas');
  fullOffscreen.width = NL_SCREEN_W;
  fullOffscreen.height = NL_SCREEN_H;
  const fCtx = fullOffscreen.getContext('2d');
  if (!fCtx) return null;
  fCtx.imageSmoothingEnabled = false;
  renderChildGameVramScene(fCtx, target, timeMs, false);

  if (!isCapturedFrameValid(fCtx, NL_SCREEN_W, NL_SCREEN_H)) {
    return null;
  }

  /* Centered aspect-preserving crop from 320x240 -> 94x68 (zero stretching) */
  const srcRatio = NL_SCREEN_W / NL_SCREEN_H;
  const dstRatio = NL_ICON_W / NL_ICON_H;
  let sx = 0;
  let sy = 0;
  let sw = NL_SCREEN_W;
  let sh = NL_SCREEN_H;
  if (srcRatio > dstRatio) {
    sw = NL_SCREEN_H * dstRatio;
    sx = (NL_SCREEN_W - sw) / 2;
  } else {
    sh = NL_SCREEN_W / dstRatio;
    sy = (NL_SCREEN_H - sh) / 2;
  }

  const coverCanvas = document.createElement('canvas');
  coverCanvas.width = NL_ICON_W;
  coverCanvas.height = NL_ICON_H;
  const iCtx = coverCanvas.getContext('2d');
  if (!iCtx) return null;
  iCtx.imageSmoothingEnabled = true;
  iCtx.drawImage(fullOffscreen, sx, sy, sw, sh, 0, 0, NL_ICON_W, NL_ICON_H);
  return coverCanvas.toDataURL('image/png');
}

export const NspireScreenCanvas = forwardRef<HTMLCanvasElement, NspireScreenCanvasProps>(
  (
    {
      state,
      scale,
      bootSeq = 0,
      onKeyCommand,
      onSelectSlot,
      onCaptureDuringGame,
      onMoveMouse,
      onClickMouse,
      onTouchpadControl,
    },
    ref
  ) => {
    const canvasRef = useRef<HTMLCanvasElement | null>(null);
    const stateRef = useRef<LauncherState>(state);
    stateRef.current = state;

    const onCaptureDuringGameRef = useRef(onCaptureDuringGame);
    onCaptureDuringGameRef.current = onCaptureDuringGame;
    const sliderDragSuppressClickRef = useRef(false);

    const imgCacheRef = useRef<Map<string, HTMLImageElement>>(imgCacheRef_global);
    const animRef = useRef({
      bootStartMs: performance.now(),
      bootDoneMs: null as number | null,
      scrollY: 0,
      dotPos: 0,
      cardScales: new Map<string, number>(),
      cardOffsetX: new Map<string, number>(),
      cardOffsetY: new Map<string, number>(),
      lastFilteredOrder: [...state.filteredIndices] as number[],
      lastToastSeq: state.toastSeq,
      toastStartMs: 0,
      activeToastText: null as string | null,
      lastRunningIdx: state.runningGameIndex,
      launchStartMs: 0,
      capturedThisLaunch: false,
      lastDisplayMode: state.displayMode,
      lastAccentColor: state.accentColor,
      inkStartMs: null as number | null,
      inkSeq: 0,
      inkField: null as Uint8Array | null,
      inkFieldMax: 200,
      inkOldData: null as Uint8ClampedArray | null,
      inkNewCanvas: null as HTMLCanvasElement | null,
      inkOutImage: null as ImageData | null,
      switchHotkeyAnim: state.settings.globalHotkey ? 1 : 0,
      switchShowNamesAnim: state.settings.showNames ? 1 : 0,
      switchShortcutsAnim: state.settings.showShortcuts ? 1 : 0,
      switchTouchpadAnim: state.settings.touchpadNav ? 1 : 0,
      switchTouchpadModeAnim: state.settings.touchpadMode === 'mouse' ? 1 : 0,
      tpTabTransAnim: state.sui.tpTab === 1 ? 1 : 0,
      tpStyleGlowAnim: 1,
      tpStyleHaloPhase: 0,
      curStylePhase: 0,
      lastTpStyleCursor: state.sui.tpStyleCursor,
      curX: 160,
      curY: 116,
      curVx: 0,
      curVy: 0,
      curOpacity: isTouchpadCursorVisible(state) ? 1 : 0,
      curIdleTicks: 0,
      curPosInit: false,
      curTrailX: [160, 160, 160, 160, 160],
      curTrailY: [116, 116, 116, 116, 116],
      curTrailAlpha: [0, 0, 0, 0, 0],
      tpSliderPos: clampTouchpadSensitivity(state.settings.touchpadSensitivity) * 256,
      tpSliderVel: 0,
      tpDetentPulse: 0,
      tpNotchLast: clampTouchpadSensitivity(state.settings.touchpadSensitivity),
      tpSliderDragging: false,
      tpSliderDragPos: clampTouchpadSensitivity(state.settings.touchpadSensitivity) * 256,
      tpSpeedSliderPos: clampCursorSpeed(state.settings.cursorSpeed) * 256,
      tpSpeedSliderVel: 0,
      tpSpeedDetentPulse: 0,
      tpSpeedNotchLast: clampCursorSpeed(state.settings.cursorSpeed),
      tpSpeedSliderDragging: false,
      tpSpeedSliderDragPos: clampCursorSpeed(state.settings.cursorSpeed) * 256,
      setOpenProgress: state.sui.screen !== 'LIBRARY' ? 1 : 0,
      lastUiScreen: state.sui.screen as SettingsScreenId,
      prevUiScreen: state.sui.screen as SettingsScreenId,
      secTransProgress: 1,
      secTransDir: 1 as 1 | -1,
      selY: 32,
      targetSelY: 32,
      selGlowProgress: 1,
      prevCursor: 0,
      lastCursorKey: '',
      listScrollPx: 0,
      targetScrollPx: 0,
      modalProgress: 0,
      modalBtnProgress: 0,
      lastModalOpen: false,
      layoutFrom: state.settings.layout as LayoutMode,
      lastLayout: state.settings.layout as LayoutMode,
      layoutTransProgress: 1,
      valPulseProgress: 0,
      valPulseDir: 1 as 1 | -1,
      lastCategory: state.activeCategory as CategoryId,
      catIndX: 34,
      catIndW: 36,
      catTransProgress: 1,
      catTransDir: 1 as 1 | -1,
      searchBarAnim: state.sui.searchActive ? 1 : 0,
      scKeyX: 34,
      scKeyY: 72,
      scKeyW: 28,
      lastScPickKey: -1,
      setDropCanvas: null as HTMLCanvasElement | null,
      setDropImage: null as ImageData | null,
      qpOpenAnim: state.sui.qpOpen ? 1 : 0,
      qpGlowX: 0,
      qpGlowY: 114,
      qpTrailX: 0,
      qpSelBounce: 0,
      qpPlusFocus: 0,
      qpSlotScales: [0.88, 0.88, 0.88, 0.88, 0.88, 0.88, 0.88, 0.88, 0.88],
      lastQpCursor: state.sui.qpCursor,
      qpGlowPhase: 0,
      lastProfSwitchSeq: state.profSwitchSeq ?? 0,
      profSwitchProgress: 1,
      profSwitchAvatarId: 0,
      profSwitchOriginX: 160,
      profSwitchOriginY: 114,
      profSwitchOriginD: 36,
      lastProfPlusSeq: state.profPlusSeq ?? 0,
      profPlusProgress: 1,
      profPlusOriginX: 160,
      profPlusOriginY: 114,
      lastProfBirthSeq: state.profBirthSeq ?? 0,
      profBirthProgress: 1,
      profBirthAvatarId: 0,
      profBirthSlot: 0,
    });

    useImperativeHandle(ref, () => canvasRef.current as HTMLCanvasElement);

    useEffect(() => {
      if (bootSeq > 0) {
        animRef.current.bootStartMs = performance.now();
        animRef.current.bootDoneMs = null;
      }
    }, [bootSeq]);

    useEffect(() => {
      for (let id = 0; id < NL_AVATAR_COUNT; id++) {
        const url = getAvatarImageUrl(id);
        if (!imgCacheRef.current.has(url)) {
          const img = new Image();
          img.src = url;
          imgCacheRef.current.set(url, img);
        }
      }
    }, []);

    useEffect(() => {
      for (const g of state.games) {
        if (g.capturedIconDataUrl && !imgCacheRef.current.has(g.capturedIconDataUrl)) {
          const img = new Image();
          img.src = g.capturedIconDataUrl;
          imgCacheRef.current.set(g.capturedIconDataUrl, img);
        }
        if (g.screenshotDataUrl && !imgCacheRef.current.has(g.screenshotDataUrl)) {
          const img = new Image();
          img.src = g.screenshotDataUrl;
          imgCacheRef.current.set(g.screenshotDataUrl, img);
        }
      }
    }, [state.games]);

    const captureCleanGameFrame = (target: GameEntry, timeMs: number): string | null => {
      /* Automatic gameplay capture is strictly for the .scr.tns launch screenshot */
      if (target.hasScreenshot && target.screenshotDataUrl) {
        return null;
      }
      return captureRealGameScreenshotDataUrl(target, timeMs);
    };

    useEffect(() => {
      let rafId = 0;

      const renderFrame = (nowMs: number) => {
        const canvas = canvasRef.current;
        if (!canvas) {
          rafId = requestAnimationFrame(renderFrame);
          return;
        }
        const ctx = canvas.getContext('2d');
        if (!ctx) {
          rafId = requestAnimationFrame(renderFrame);
          return;
        }
        ctx.imageSmoothingEnabled = false;

        const s = stateRef.current;
        const anim = animRef.current;

        if (s.toastSeq !== anim.lastToastSeq && s.toastMessage) {
          anim.lastToastSeq = s.toastSeq;
          anim.activeToastText = s.toastMessage;
          anim.toastStartMs = nowMs;
        }

        if (s.runningGameIndex !== null && anim.lastRunningIdx === null) {
          anim.launchStartMs = nowMs;
          anim.capturedThisLaunch = false;
          /* Freeze all card slot offsets during launch so zero items move */
          anim.cardOffsetX.clear();
          anim.cardOffsetY.clear();
          anim.lastFilteredOrder = [...s.filteredIndices];
        } else if (s.runningGameIndex === null) {
          anim.capturedThisLaunch = false;
        }
        anim.lastRunningIdx = s.runningGameIndex;

        /* Synchronized startup animation matching hardware C renderer:
         * - Loads saved displayMode + accentColor immediately from frame 0
         * - Zoom pop-in (0..240ms: 24 -> 36px) + smooth cosine breathing (36 <-> 44px)
         * - Compact circular 8-dot loader in saved accent color
         * - Ends cleanly via 80ms crossfade once assets are ready */
        const bootElapsed = nowMs - anim.bootStartMs;
        if (s.runningGameIndex === null) {
          let assetsReady = true;
          for (const g of s.games) {
            if (g.iconSource !== 'NONE' && g.capturedIconDataUrl) {
              const cached = imgCacheRef.current.get(g.capturedIconDataUrl);
              if (cached && !cached.complete) {
                assetsReady = false;
                break;
              }
            }
          }
          if (assetsReady && bootElapsed >= 540 && anim.bootDoneMs === null) {
            anim.bootDoneMs = nowMs;
          }
          const transElapsed = anim.bootDoneMs !== null ? nowMs - anim.bootDoneMs : -1;

          if (anim.bootDoneMs === null) {
            anim.lastDisplayMode = s.displayMode;
            anim.lastAccentColor = s.accentColor;
            anim.inkStartMs = null;
            drawStartupSplashView(ctx, s, bootElapsed, true);
            rafId = requestAnimationFrame(renderFrame);
            return;
          }

          if (transElapsed < 85) {
            anim.lastDisplayMode = s.displayMode;
            anim.lastAccentColor = s.accentColor;
            anim.inkStartMs = null;
            const t = Math.min(1, transElapsed / 85);
            const alpha = 1 - (1 - t) * (1 - t);
            drawLauncherView(ctx, s, anim, nowMs, imgCacheRef.current, true);
            ctx.save();
            ctx.globalAlpha = 1 - alpha;
            drawStartupSplashView(ctx, s, anim.bootDoneMs - anim.bootStartMs, false);
            ctx.restore();
            rafId = requestAnimationFrame(renderFrame);
            return;
          }
        }

        /* 4-Stage Synchronized Launch Transition (0..760ms) mirroring native/renderer.c:
         * 1. Upon releasing EXE, the selected custom icon shrinks smoothly (rawT in 0..36/256: scale 280/256 -> 232/256),
         *    while all other library icons progressively fade out toward 0% opacity (rawT in 0..180/256).
         * 2. As the selected icon begins its slight rebound (rawT >= 36/256), 3 concentric luminous water-drop
         *    ripples emerge from its exact center (cardCx, cardCy) strictly BEHIND the custom icon.
         * 3. During expT in 0..136/256, the custom icon stays in the foreground at (cardCx, cardCy) while the
         *    3 concentric ripples propagate clearly around its perimeter.
         * 4. During expT in 136/256..1.0, a smooth crossfade replaces the custom icon with the screenshot (.scr.tns)
         *    while the card completes its smooth expansion to fullscreen (320x240). */
        if (s.runningGameIndex !== null && s.games[s.runningGameIndex]) {
          anim.lastDisplayMode = s.displayMode;
          anim.lastAccentColor = s.accentColor;
          anim.inkStartMs = null;
          const runningGame = s.games[s.runningGameIndex];
          const launchElapsed = nowMs - anim.launchStartMs;
          const LAUNCH_DURATION_MS = 760;
          if (launchElapsed < LAUNCH_DURATION_MS) {
            const launchTheme = getConsoleTheme(s.displayMode, s.accentColor);
            const rawT = Math.max(0, Math.min(1, launchElapsed / LAUNCH_DURATION_MS));
            const SHRINK_END_T = 36 / 256; // ~0.1406
            const FADE_OUT_END_T = 180 / 256; // ~0.703

            /* Progressive, slow, smooth fade-out of all non-selected library icons (1.0 -> 0.0) */
            const otherCardsAlpha =
              rawT >= FADE_OUT_END_T
                ? 0
                : 0.5 + 0.5 * Math.cos(Math.PI * Math.min(1, rawT / FADE_OUT_END_T));

            /* LAYER 1 — Arrière-plan : Library UI + non-selected icons fading out progressively */
            drawLauncherView(
              ctx,
              s,
              anim,
              nowMs,
              imgCacheRef.current,
              false,
              s.runningGameIndex,
              otherCardsAlpha
            );

            /* Gentle backdrop dim (subtle during ripple propagation, stronger during final expansion) */
            const dimAlpha =
              rawT <= 140 / 256
                ? (rawT / (140 / 256)) * 0.18
                : 0.18 + ((rawT - 140 / 256) / (1 - 140 / 256)) * 0.5;
            if (dimAlpha > 0.004) {
              ctx.fillStyle = launchTheme.isLight
                ? `rgba(216, 222, 232, ${dimAlpha})`
                : `rgba(6, 8, 12, ${dimAlpha})`;
              ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);
            }

            const m = getLayoutMetrics(
              s.settings.layout,
              s.settings.showNames,
              s.settings.showShortcuts,
              s.filteredIndices.length
            );
            const searchH = Math.round(28 * anim.searchBarAnim);
            let slotPos = s.selectedPos;
            if (
              slotPos < 0 ||
              slotPos >= s.filteredIndices.length ||
              s.filteredIndices[slotPos] !== s.runningGameIndex
            ) {
              const foundSlot = s.filteredIndices.indexOf(s.runningGameIndex);
              if (foundSlot >= 0) slotPos = foundSlot;
            }
            const row = Math.floor(slotPos / m.cols);
            const slotCoords = computeGridSlotPos(m, s.filteredIndices.length, slotPos);
            const baseX = slotCoords.x;
            let baseY = slotCoords.y + searchH - Math.round(anim.scrollY);
            if (searchH === 0 && baseY < NL_VIEW_TOP && row === s.currentPage * m.rows) {
              baseY = NL_VIEW_TOP + 1;
            }
            const cardCx = baseX + m.cardW / 2;
            const cardCy = baseY + m.cardH / 2;

            const selScale = 280 / 256;
            const shrinkScale = 232 / 256;
            const minW = m.cardW * shrinkScale;
            const minH = m.cardH * shrinkScale;
            const minX = cardCx - minW / 2;
            const minY = cardCy - minH / 2;

            let rw = minW;
            let rh = minH;
            let rx = minX;
            let ry = minY;
            let rad = m.radius;
            let expT = 0;
            let ease = 0;

            if (rawT < SHRINK_END_T) {
              /* Stage 1: Custom icon shrinks smoothly (280/256 -> 232/256) */
              const st = rawT / SHRINK_END_T;
              const sEase = st * (2 - st);
              const curScale = selScale + (shrinkScale - selScale) * sEase;
              rw = m.cardW * curScale;
              rh = m.cardH * curScale;
              rx = cardCx - rw / 2;
              ry = cardCy - rh / 2;
              rad = m.radius;
            } else {
              /* Stage 2 & 3 (expT in 0..144/256): Slight rebound (232/256 -> ~256/256, ease 0..20/256)
               * keeping the custom icon localized at (cardCx, cardCy) while the 3 concentric ripples
               * propagate clearly behind and around it.
               * Stage 4 (expT in 144/256..1.0): Smooth cubic expansion to fullscreen 320x240. */
              expT = Math.min(1, (rawT - SHRINK_END_T) / (1 - SHRINK_END_T));
              const reboundHoldEnd = 144 / 256;
              const reboundMaxEase = 20 / 256;
              if (expT < reboundHoldEnd) {
                const p = expT / reboundHoldEnd;
                const pEase = p * (2 - p);
                ease = reboundMaxEase * pEase;
              } else {
                const p = (expT - reboundHoldEnd) / (1 - reboundHoldEnd);
                const pCubic = 1 - Math.pow(1 - p, 3);
                ease = reboundMaxEase + (1 - reboundMaxEase) * pCubic;
              }
              rw = minW + (NL_SCREEN_W - minW) * ease;
              rh = minH + (NL_SCREEN_H - minH) * ease;
              rx = minX + (0 - minX) * ease;
              ry = minY + (0 - minY) * ease;
              rad = Math.max(0, m.radius * (1 - Math.min(1, ease)));
            }

            /* LAYER 2 (Plan intermédiaire — DERRIÈRE l'icône personnalisée) :
             * Exactement 3 ondes concentriques (goutte d'eau / Switch-inspired) :
             * - Partent dynamiquement du centre exact (cardCx, cardCy) de l'icône sélectionnée
             * - Dessinées AVANT l'icône personnalisée pour rester strictement derrière elle
             * - Se propagent autour de l'icône sans jamais atteindre les bords de l'écran */
            if (rawT >= SHRINK_END_T && expT < 228 / 256) {
              const waveDelays = [0.0, 22 / 256, 44 / 256];
              const waveLives = [156 / 256, 148 / 256, 140 / 256];
              const waveSpanPx = [25, 19, 13];
              const wavePeakAlphas = [0.6, 0.66, 0.72];
              const waveHalfWidths = [5.5, 5.25, 5.0];

              const minDim = Math.min(m.cardW, m.cardH);
              const halfCard = Math.max(14, (minDim * 236) / 512);
              const rStart = Math.max(4, halfCard - 7);

              ctx.save();
              /* Strict 4px safety margin from all 4 screen borders so ripples never touch screen edges */
              ctx.beginPath();
              ctx.rect(4, 4, NL_SCREEN_W - 8, NL_SCREEN_H - 8);
              ctx.clip();

              for (let w = 0; w < 3; w++) {
                const delay = waveDelays[w];
                const life = waveLives[w];
                if (expT <= delay || expT >= delay + life) continue;

                const st = (expT - delay) / life;
                const expandEase = 1 - Math.pow(1 - st, 3);
                const rEnd = halfCard + waveSpanPx[w];
                const radius = rStart + (rEnd - rStart) * expandEase;

                let env = 0;
                const fadeInEnd = 40 / 256;
                if (st < fadeInEnd) {
                  env = 0.5 - 0.5 * Math.cos(Math.PI * (st / fadeInEnd));
                } else {
                  const norm = (1 - st) / (1 - fadeInEnd);
                  env = norm * norm;
                }
                const alpha = env * wavePeakAlphas[w];
                if (alpha <= 0.01) continue;

                const hw = waveHalfWidths[w];
                const rInner = Math.max(0, radius - hw);
                const rOuter = radius + hw;
                /* Launch animation glows are strictly pure white across all themes (Blue, Red, Green, Yellow) */
                const haloCol = '#ffffff';
                const crestCol = '#ffffff';

                ctx.save();
                ctx.globalAlpha = alpha;
                const grad = ctx.createRadialGradient(
                  cardCx,
                  cardCy,
                  rInner,
                  cardCx,
                  cardCy,
                  rOuter
                );
                grad.addColorStop(0, 'rgba(0,0,0,0)');
                grad.addColorStop(0.28, haloCol);
                grad.addColorStop(0.5, crestCol);
                grad.addColorStop(0.72, haloCol);
                grad.addColorStop(1, 'rgba(0,0,0,0)');
                ctx.fillStyle = grad;
                ctx.beginPath();
                ctx.arc(cardCx, cardCy, rOuter, 0, Math.PI * 2);
                ctx.fill();
                ctx.restore();
              }
              ctx.restore();
            }

            /* LAYER 3 (Premier plan — DEVANT les lueurs concentriques) :
             * Lueur douce blanche de bordure + Icône personnalisée du jeu sélectionné +
             * Fondu enchaîné vers la capture d'écran (.scr.tns) en Étape 4 (expT >= 136/256) */
            if (rawT >= SHRINK_END_T) {
              const glowRise = Math.min(1, expT / 0.17);
              const borderAlpha = Math.max(0, glowRise * (1 - ease) * 0.78);
              if (borderAlpha > 0.02 && rad > 0.5) {
                ctx.save();
                ctx.globalAlpha = borderAlpha * 0.55;
                traceRoundedRect(ctx, rx - 1.5, ry - 1.5, rw + 3, rh + 3, rad + 1.5);
                ctx.strokeStyle = '#ffffff';
                ctx.lineWidth = 2.2;
                ctx.stroke();

                ctx.globalAlpha = borderAlpha;
                traceRoundedRect(ctx, rx + 0.5, ry + 0.5, rw - 1, rh - 1, rad);
                ctx.strokeStyle = '#ffffff';
                ctx.lineWidth = 1.3;
                ctx.stroke();
                ctx.restore();
              }
            }

            const scrUrl =
              runningGame.hasScreenshot && runningGame.screenshotDataUrl
                ? runningGame.screenshotDataUrl
                : null;
            const scrImg = scrUrl ? imgCacheRef.current.get(scrUrl) : undefined;
            const hasReadyScr = Boolean(
              scrImg && scrImg.complete && scrImg.naturalWidth > 0
            );

            const xfadeStartExpT = 136 / 256;
            const xfadeEndExpT = 212 / 256;

            if (hasReadyScr && scrImg && rawT >= SHRINK_END_T && expT > xfadeStartExpT) {
              /* Stage 4: Smoothstep crossfade alpha (0 -> 1 over expT in [136/256..212/256]) */
              const u = Math.max(
                0,
                Math.min(1, (expT - xfadeStartExpT) / (xfadeEndExpT - xfadeStartExpT))
              );
              const xfadeAlpha = u * u * (3 - 2 * u);

              /* Draw custom icon in foreground first so it masks the inner area of the ripples */
              if (xfadeAlpha < 0.995) {
                drawGameCoverCard(
                  ctx,
                  rx,
                  ry,
                  rw,
                  rh,
                  rad,
                  runningGame,
                  false,
                  0,
                  launchTheme,
                  imgCacheRef.current
                );
              }

              /* Superimpose the real captured gameplay screenshot fading in (0 -> 1) */
              if (xfadeAlpha > 0.005) {
                ctx.save();
                traceRoundedRect(ctx, rx, ry, rw, rh, rad);
                ctx.clip();
                ctx.globalAlpha = xfadeAlpha;
                ctx.imageSmoothingEnabled = true;
                ctx.imageSmoothingQuality = 'high';
                ctx.drawImage(scrImg, rx, ry, rw, rh);
                ctx.imageSmoothingEnabled = false;
                ctx.restore();
              }
            } else {
              /* Stage 1..3 (or fallback when no screenshot exists): draw custom icon 100% opaque in front of ripples */
              drawGameCoverCard(
                ctx,
                rx,
                ry,
                rw,
                rh,
                rad,
                runningGame,
                false,
                0,
                launchTheme,
                imgCacheRef.current
              );
            }

            rafId = requestAnimationFrame(renderFrame);
            return;
          }

          renderChildGameVramScene(ctx, runningGame, nowMs, true);

          /* Automatic one-shot capture DURING active gameplay once graphics have settled (>= 950ms):
           * Saves exclusively to the launch screenshot (.scr.tns), never overwriting custom library icons */
          if (!anim.capturedThisLaunch && launchElapsed >= 950) {
            anim.capturedThisLaunch = true;
            if (!runningGame.hasScreenshot || !runningGame.screenshotDataUrl) {
              const captured = captureCleanGameFrame(runningGame, nowMs);
              if (captured) {
                onCaptureDuringGameRef.current(s.runningGameIndex, captured);
              }
            }
          }

          rafId = requestAnimationFrame(renderFrame);
          return;
        }

        /* Trigger organic ink diffusion from screen center on DOCK (displayMode) or MENU (accentColor) */
        if (s.displayMode !== anim.lastDisplayMode || s.accentColor !== anim.lastAccentColor) {
          const prevSnap = ctx.getImageData(0, 0, NL_SCREEN_W, NL_SCREEN_H).data;
          if (!anim.inkOldData || anim.inkOldData.length !== prevSnap.length) {
            anim.inkOldData = new Uint8ClampedArray(prevSnap.length);
          }
          anim.inkOldData.set(prevSnap);
          anim.inkSeq += 1;
          const built = buildOrganicInkFieldJs(anim.inkSeq);
          anim.inkField = built.field;
          anim.inkFieldMax = built.maxVal;
          anim.inkStartMs = nowMs;
          anim.lastDisplayMode = s.displayMode;
          anim.lastAccentColor = s.accentColor;
        }

        const INK_DURATION_MS = 324;
        if (anim.inkStartMs !== null && anim.inkOldData && anim.inkField) {
          const inkElapsed = nowMs - anim.inkStartMs;
          if (inkElapsed >= INK_DURATION_MS) {
            anim.inkStartMs = null;
          } else {
            if (!anim.inkNewCanvas) {
              anim.inkNewCanvas = document.createElement('canvas');
              anim.inkNewCanvas.width = NL_SCREEN_W;
              anim.inkNewCanvas.height = NL_SCREEN_H;
            }
            const newCtx = anim.inkNewCanvas.getContext('2d');
            if (newCtx) {
              newCtx.imageSmoothingEnabled = false;
              drawLauncherView(newCtx, s, anim, nowMs, imgCacheRef.current, true);
              const newData = newCtx.getImageData(0, 0, NL_SCREEN_W, NL_SCREEN_H).data;
              if (!anim.inkOutImage) {
                anim.inkOutImage = ctx.createImageData(NL_SCREEN_W, NL_SCREEN_H);
              }
              const out = anim.inkOutImage.data;
              const oldData = anim.inkOldData;
              const field = anim.inkField;
              const t = Math.max(0, Math.min(1, inkElapsed / INK_DURATION_MS));
              const inv = 1 - t;
              const cubic = 1 - inv * inv * inv;
              const ease = t * 0.3 + cubic * 0.7;
              const edgeW = 14;
              const front = (anim.inkFieldMax + edgeW + 2) * ease;
              const totalPx = NL_SCREEN_W * NL_SCREEN_H;

              for (let p = 0; p < totalPx; p++) {
                const delta = front - field[p];
                const idx = p << 2;
                if (delta >= edgeW) {
                  out[idx] = newData[idx];
                  out[idx + 1] = newData[idx + 1];
                  out[idx + 2] = newData[idx + 2];
                  out[idx + 3] = 255;
                } else if (delta <= 0) {
                  out[idx] = oldData[idx];
                  out[idx + 1] = oldData[idx + 1];
                  out[idx + 2] = oldData[idx + 2];
                  out[idx + 3] = 255;
                } else {
                  const a = delta / edgeW;
                  out[idx] = Math.round(oldData[idx] + (newData[idx] - oldData[idx]) * a);
                  out[idx + 1] = Math.round(oldData[idx + 1] + (newData[idx + 1] - oldData[idx + 1]) * a);
                  out[idx + 2] = Math.round(oldData[idx + 2] + (newData[idx + 2] - oldData[idx + 2]) * a);
                  out[idx + 3] = 255;
                }
              }

              ctx.putImageData(anim.inkOutImage, 0, 0);
              rafId = requestAnimationFrame(renderFrame);
              return;
            }
          }
        }

        drawLauncherView(ctx, s, anim, nowMs, imgCacheRef.current, true);
        rafId = requestAnimationFrame(renderFrame);
      };

      rafId = requestAnimationFrame(renderFrame);
      return () => cancelAnimationFrame(rafId);
    }, []);

    const handleCanvasClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
      const canvas = canvasRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const x = ((e.clientX - rect.left) / rect.width) * NL_SCREEN_W;
      const y = ((e.clientY - rect.top) / rect.height) * NL_SCREEN_H;

      if (sliderDragSuppressClickRef.current) {
        sliderDragSuppressClickRef.current = false;
        return;
      }

      /* Exit game cleanly with ZERO capture on exit */
      if (state.runningGameIndex !== null) {
        onKeyCommand('ESC');
        return;
      }

      /* Mode 2 (Souris) or active CTRL hold routes canvas clicks directly through clickMouse() */
      if (isMouseModeActive(state) && onClickMouse) {
        onMoveMouse?.(Math.round(x), Math.round(y));
        onClickMouse();
        return;
      }

      /* Top-right Gear Button toggles Settings cleanly */
      if (
        y <= NL_TOP_BAR_H + 2 &&
        x >= NL_GEAR_BTN_X - 4 &&
        x <= NL_GEAR_BTN_X + NL_GEAR_BTN_D + 6
      ) {
        onKeyCommand('SETTINGS');
        return;
      }

      if (state.sui.screen !== 'LIBRARY') {
        if (y <= NL_TOP_BAR_H) {
          /* Clicking the discreet Power button next to the Settings title triggers Power-Off (DEL) */
          if (x >= 90 && x <= 246) {
            onKeyCommand('RESCAN');
          } else {
            onKeyCommand('ESC');
          }
          return;
        }
        if (state.sui.screen === 'SETTINGS_TOUCHPAD') {
          if (y >= 28 && y <= 44) {
            const targetTab: 0 | 1 = x >= 155 ? 1 : 0;
            if (targetTab !== state.sui.tpTab) {
              onKeyCommand('TAB');
            }
            return;
          }
          if (state.sui.tpTab === 1) {
            if (y >= 50 && y <= 166) {
              const cardIdx = x < 110 ? 0 : x < 211 ? 1 : 2;
              const chosen = cardIndexToCursorStyle(cardIdx);
              if (onTouchpadControl) {
                onTouchpadControl(
                  state.sui.tpCursor as 0 | 1 | 2 | 3 | 4,
                  undefined,
                  false,
                  undefined,
                  chosen,
                  1
                );
              } else {
                const diff = cardIdx - state.sui.tpStyleCursor;
                for (let i = 0; i < Math.abs(diff); i++) {
                  onKeyCommand(diff > 0 ? 'RIGHT' : 'LEFT');
                }
                onKeyCommand('EXE');
              }
            }
            return;
          }
          /* Row 0 (y=47..75): Mode de navigation (Pave tactile / Souris) */
          if (y >= 47 && y <= 75) {
            if (onTouchpadControl) {
              onTouchpadControl(0, undefined, false, undefined, undefined, 0, true);
            } else {
              onKeyCommand('EXE');
            }
            return;
          }
          /* Row 1 (y=77..104): Glissement au pave (Mode 1) */
          if (y >= 77 && y <= 104) {
            if (onTouchpadControl) {
              onTouchpadControl(1, undefined, true);
            } else {
              onKeyCommand('EXE');
            }
            return;
          }
          /* Row 2 (y=106..144): Sensibilite pave (5 crans) */
          if (y >= 106 && y <= 144) {
            const rawNotch = Math.max(0, Math.min(4, Math.round(((x - 34) / 252) * 4)));
            if (onTouchpadControl) {
              onTouchpadControl(2, rawNotch, false);
            } else {
              const diff = rawNotch - state.settings.touchpadSensitivity;
              for (let i = 0; i < Math.abs(diff); i++) {
                onKeyCommand(diff > 0 ? 'RIGHT' : 'LEFT');
              }
            }
            return;
          }
          /* Row 3 (y=146..184): Vitesse souris (5 crans) */
          if (y >= 146 && y <= 184) {
            const rawNotch = Math.max(0, Math.min(4, Math.round(((x - 34) / 252) * 4)));
            if (onTouchpadControl) {
              onTouchpadControl(3, undefined, false, rawNotch);
            } else {
              const diff = rawNotch - state.settings.cursorSpeed;
              for (let i = 0; i < Math.abs(diff); i++) {
                onKeyCommand(diff > 0 ? 'RIGHT' : 'LEFT');
              }
            }
            return;
          }
          /* Row 4 (y=186..217): Apparence du curseur */
          if (y >= 186 && y <= 217) {
            onKeyCommand('TAB');
            return;
          }
          return;
        }
        if (state.sui.screen === 'SETTINGS_DISPLAY') {
          if (y >= 29 && y <= 59) {
            onKeyCommand(x < 210 ? 'LEFT' : 'RIGHT');
            return;
          }
          if (y >= 60 && y <= 90) {
            if (state.sui.displayCursor === 0) onKeyCommand('DOWN');
            else if (state.sui.displayCursor === 2) onKeyCommand('UP');
            onKeyCommand('EXE');
            return;
          }
          if (y >= 91 && y <= 121) {
            if (state.sui.displayCursor === 0) onKeyCommand('UP');
            else if (state.sui.displayCursor === 1) onKeyCommand('DOWN');
            onKeyCommand('EXE');
            return;
          }
        }
        if (state.sui.screen === 'SETTINGS_MAIN' && y >= 32 && y <= 212) {
          const clickedRow = Math.floor((y - 32) / 30) + state.sui.menuScroll;
          if (clickedRow >= 0 && clickedRow < SETTINGS_MENU_ITEMS.length) {
            const diff = clickedRow - state.sui.menuCursor;
            if (diff < 0) {
              for (let i = 0; i < -diff; i++) onKeyCommand('UP');
            } else if (diff > 0) {
              for (let i = 0; i < diff; i++) onKeyCommand('DOWN');
            }
            onKeyCommand('EXE');
          }
          return;
        }
        if (state.sui.screen === 'SETTINGS_CAPTURE_BATCH' && y >= 170 && y <= 206) {
          if (state.sui.capActive) {
            if (x < 160) onKeyCommand('RIGHT');
            else onKeyCommand('ESC');
          } else {
            onKeyCommand('EXE');
          }
          return;
        }
        if (state.sui.screen === 'SETTINGS_ICONS') {
          if (state.sui.iconConfirmModal && y >= 134 && y <= 174) {
            if (x < 160) {
              onKeyCommand('ESC');
            } else {
              if (!state.sui.iconConfirmYes) onKeyCommand('RIGHT');
              onKeyCommand('EXE');
            }
            return;
          }
          if (!state.sui.iconConfirmModal && y >= 32 && y <= 214) {
            const c = Math.floor((x - 21) / 90);
            const r = Math.floor((y - 33) / 90) + state.sui.iconScrollRow;
            if (c >= 0 && c < 3 && r >= 0) {
              const targetIdx = r * 3 + c;
              if (targetIdx >= 0 && targetIdx < state.games.length) {
                const diff = targetIdx - state.sui.iconCursor;
                if (diff < 0) {
                  for (let i = 0; i < -diff; i++) onKeyCommand('LEFT');
                } else if (diff > 0) {
                  for (let i = 0; i < diff; i++) onKeyCommand('RIGHT');
                }
                onKeyCommand('EXE');
              }
            }
            return;
          }
        }
        if (state.sui.screen === 'SETTINGS_PROFILES') {
          const mode = state.sui.profMode;
          if (mode === 'LIST') {
            if (y >= 32 && y <= 212) {
              const totalRows = state.profiles.items.length + 1;
              const clickedRow = Math.floor((y - 32) / 30) + state.sui.profScroll;
              if (clickedRow >= 0 && clickedRow < totalRows) {
                const diff = clickedRow - state.sui.profCursor;
                for (let i = 0; i < Math.abs(diff); i++) {
                  onKeyCommand(diff > 0 ? 'DOWN' : 'UP');
                }
                onKeyCommand('EXE');
              }
            }
            return;
          }
          if (mode === 'DETAIL') {
            if (y >= 82 && y <= 212) {
              const row = Math.floor((y - 83) / 32);
              if (row >= 0 && row < 4) {
                const diff = row - state.sui.profDetailCursor;
                for (let i = 0; i < Math.abs(diff); i++) {
                  onKeyCommand(diff > 0 ? 'DOWN' : 'UP');
                }
                onKeyCommand('EXE');
              }
            }
            return;
          }
          if (mode === 'EDIT_AVATAR' || mode === 'CREATE_AVATAR') {
            if (y >= 28 && y <= 48 && x >= 11 && x <= 308) {
              const targetCat = Math.min(3, Math.max(0, Math.floor((x - 11) / 75)));
              const tabSteps = (targetCat - state.sui.profAvatarCat + NL_AVATAR_CAT_COUNT) % NL_AVATAR_CAT_COUNT;
              for (let i = 0; i < tabSteps; i++) onKeyCommand('TAB');
              return;
            }
            const cat = state.sui.profAvatarCat;
            const { count: ccnt } = getAvatarCategoryRange(cat);
            const cols = cat === 0 ? 8 : 5;
            const diam = cat === 0 ? 31 : 36;
            const dxStep = cat === 0 ? 37 : 52;
            const dyStep = cat === 0 ? 54 : 58;
            const gridW = (cols - 1) * dxStep + diam;
            const startX = Math.floor((NL_SCREEN_W - gridW) / 2);
            const startY = 58;
            for (let i = 0; i < ccnt; i++) {
              const r = Math.floor(i / cols);
              const c = i % cols;
              const ax = startX + c * dxStep;
              const ay = startY + r * dyStep;
              if (x >= ax - 3 && x <= ax + diam + 3 && y >= ay - 3 && y <= ay + diam + 6) {
                const curR = Math.floor(state.sui.profAvatarCursor / cols);
                const curC = state.sui.profAvatarCursor % cols;
                const dRow = r - curR;
                const dCol = c - curC;
                for (let k = 0; k < Math.abs(dRow); k++) onKeyCommand(dRow > 0 ? 'DOWN' : 'UP');
                for (let k = 0; k < Math.abs(dCol); k++) onKeyCommand(dCol > 0 ? 'RIGHT' : 'LEFT');
                onKeyCommand('EXE');
                return;
              }
            }
            return;
          }
          if (mode === 'CONFIRM_DELETE' && y >= 144 && y <= 178) {
            if (x < 160) {
              onKeyCommand('ESC');
            } else {
              if (!state.sui.profDeleteYes) onKeyCommand('RIGHT');
              onKeyCommand('EXE');
            }
            return;
          }
          return;
        }
        if (state.sui.screen === 'SETTINGS_RESET' && y >= 162 && y <= 196) {
          if (x < 160) {
            onKeyCommand('ESC');
          } else {
            if (!state.sui.resetConfirmYes) onKeyCommand('RIGHT');
            onKeyCommand('EXE');
          }
          return;
        }
        if (state.sui.screen === 'SETTINGS_INFO' || state.sui.screen === 'SETTINGS_CREDITS') {
          onKeyCommand('ESC');
          return;
        }
        return;
      }

      if (state.sui.qpOpen) {
        if (x < 10 || x > 310 || y < 52 || y > 188) {
          onKeyCommand('ESC');
          return;
        }
        const pcount = Math.max(1, state.profiles.items.length);
        const totalSlots = pcount + 1;
        const visMax = 6;
        const diam = 34;
        const stride = 44;
        const cur = Math.min(totalSlots - 1, Math.max(0, state.sui.qpCursor));
        const shown = Math.min(visMax, totalSlots);
        let winStart = 0;
        if (totalSlots > visMax) {
          winStart = cur - Math.floor(visMax / 2);
          if (winStart < 0) winStart = 0;
          if (winStart > totalSlots - visMax) winStart = totalSlots - visMax;
        }
        const stripW = shown * diam + (shown - 1) * (stride - diam);
        const startX = 160 - Math.floor(stripW / 2);
        const cy = 114;
        for (let v = 0; v < shown; v++) {
          const slot = winStart + v;
          const sx = startX + v * stride;
          const sy = cy - Math.floor(diam / 2);
          if (x >= sx - 4 && x <= sx + diam + 4 && y >= sy - 10 && y <= sy + diam + 22) {
            const diff = slot - cur;
            for (let k = 0; k < Math.abs(diff); k++) onKeyCommand(diff > 0 ? 'RIGHT' : 'LEFT');
            onKeyCommand('EXE');
            return;
          }
        }
        return;
      }

      if (y <= NL_TOP_BAR_H && x <= 30) {
        onKeyCommand('PROFILE_QUICK');
        return;
      }

      if (y <= NL_TOP_BAR_H && x >= 148 && x <= 166) {
        onKeyCommand('FAV');
        return;
      }

      if (y <= NL_TOP_BAR_H && x >= 32 && x <= 144) {
        onKeyCommand('TAB');
        return;
      }

      const mClick = getLayoutMetrics(
        state.settings.layout,
        state.settings.showNames,
        state.settings.showShortcuts,
        state.filteredIndices.length
      );
      if (y >= NL_VIEW_TOP && y < mClick.viewBottom) {
        const scrollY = state.currentPage * mClick.pageStrideY;
        const searchOffset = state.sui.searchActive ? 28 : 0;
        for (let fIdx = 0; fIdx < state.filteredIndices.length; fIdx++) {
          const slotCoords = computeGridSlotPos(mClick, state.filteredIndices.length, fIdx);
          const cx = slotCoords.x;
          const cy = slotCoords.y + searchOffset - scrollY;
          if (
            x >= cx &&
            x <= cx + mClick.cardW &&
            y >= cy &&
            y <= cy + mClick.cardH + mClick.labelH
          ) {
            onSelectSlot(fIdx);
            return;
          }
        }
      }
    };

    const computeDetentDragPos = (canvasX: number): { notch: number; warpedPosFp: number } => {
      const norm = Math.max(0, Math.min(1, (canvasX - 34) / 252));
      const rawNotch = norm * 4;
      const nearest = Math.round(rawNotch);
      const d = rawNotch - nearest;
      /* Subtle magnetic detent catch: gentle resistance near notch centers before rolling cleanly to next notch */
      const absD = Math.abs(d);
      const warpedD = Math.sign(d) * 0.5 * Math.pow(Math.min(1, absD * 2), 1.85);
      const warpedNotch = Math.max(0, Math.min(4, nearest + warpedD));
      return { notch: nearest, warpedPosFp: warpedNotch * 256 };
    };

    const handleCanvasPointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
      const canvas = canvasRef.current;
      if (!canvas) return;
      const s = stateRef.current;
      if (s.runningGameIndex !== null || s.sui.screen !== 'SETTINGS_TOUCHPAD') return;
      const rect = canvas.getBoundingClientRect();
      const x = ((e.clientX - rect.left) / Math.max(1, rect.width)) * NL_SCREEN_W;
      const y = ((e.clientY - rect.top) / Math.max(1, rect.height)) * NL_SCREEN_H;
      if (s.sui.tpTab === 0 && y >= 116 && y <= 144) {
        e.currentTarget.setPointerCapture(e.pointerId);
        const { notch, warpedPosFp } = computeDetentDragPos(x);
        animRef.current.tpSliderDragging = true;
        animRef.current.tpSliderDragPos = warpedPosFp;
        animRef.current.tpSliderPos = warpedPosFp;
        sliderDragSuppressClickRef.current = true;
        if (notch !== animRef.current.tpNotchLast) {
          animRef.current.tpNotchLast = notch;
          animRef.current.tpDetentPulse = 1;
        }
        onTouchpadControl?.(2, notch, false);
      } else if (s.sui.tpTab === 0 && y >= 156 && y <= 184) {
        e.currentTarget.setPointerCapture(e.pointerId);
        const { notch, warpedPosFp } = computeDetentDragPos(x);
        animRef.current.tpSpeedSliderDragging = true;
        animRef.current.tpSpeedSliderDragPos = warpedPosFp;
        animRef.current.tpSpeedSliderPos = warpedPosFp;
        sliderDragSuppressClickRef.current = true;
        if (notch !== animRef.current.tpSpeedNotchLast) {
          animRef.current.tpSpeedNotchLast = notch;
          animRef.current.tpSpeedDetentPulse = 1;
        }
        onTouchpadControl?.(3, undefined, false, notch);
      }
    };

    const handleCanvasPointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
      const canvas = canvasRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      const x = ((e.clientX - rect.left) / Math.max(1, rect.width)) * NL_SCREEN_W;
      const y = ((e.clientY - rect.top) / Math.max(1, rect.height)) * NL_SCREEN_H;

      if (animRef.current.tpSliderDragging) {
        const { notch, warpedPosFp } = computeDetentDragPos(x);
        animRef.current.tpSliderDragPos = warpedPosFp;
        animRef.current.tpSliderPos = warpedPosFp;
        if (notch !== animRef.current.tpNotchLast) {
          animRef.current.tpNotchLast = notch;
          animRef.current.tpDetentPulse = 1;
          onTouchpadControl?.(2, notch, false);
        }
        return;
      }
      if (animRef.current.tpSpeedSliderDragging) {
        const { notch, warpedPosFp } = computeDetentDragPos(x);
        animRef.current.tpSpeedSliderDragPos = warpedPosFp;
        animRef.current.tpSpeedSliderPos = warpedPosFp;
        if (notch !== animRef.current.tpSpeedNotchLast) {
          animRef.current.tpSpeedNotchLast = notch;
          animRef.current.tpSpeedDetentPulse = 1;
          onTouchpadControl?.(3, undefined, false, notch);
        }
        return;
      }

      const s = stateRef.current;
      if (isMouseModeActive(s) && s.runningGameIndex === null && onMoveMouse) {
        const clampedX = Math.max(0, Math.min(NL_SCREEN_W - 1, Math.round(x)));
        const clampedY = Math.max(0, Math.min(NL_SCREEN_H - 1, Math.round(y)));
        onMoveMouse(clampedX, clampedY);
      }
    };

    const handleCanvasPointerUp = () => {
      if (animRef.current.tpSliderDragging) {
        animRef.current.tpSliderDragging = false;
        animRef.current.tpDetentPulse = Math.max(animRef.current.tpDetentPulse, 0.85);
      }
      if (animRef.current.tpSpeedSliderDragging) {
        animRef.current.tpSpeedSliderDragging = false;
        animRef.current.tpSpeedDetentPulse = Math.max(animRef.current.tpSpeedDetentPulse, 0.85);
      }
    };

    return (
      <div className="relative inline-block rounded-lg bg-slate-950 p-2 border border-slate-800 shadow-inner">
        <canvas
          ref={canvasRef}
          width={NL_SCREEN_W}
          height={NL_SCREEN_H}
          onClick={handleCanvasClick}
          onPointerDown={handleCanvasPointerDown}
          onPointerMove={handleCanvasPointerMove}
          onPointerUp={handleCanvasPointerUp}
          onPointerCancel={handleCanvasPointerUp}
          data-testid="nspire-framebuffer-canvas"
          className={`pixelated-canvas block rounded border border-slate-800 ${
            isMouseModeActive(state) ? 'cursor-none' : 'cursor-pointer'
          } ${scale === 2 ? 'w-[640px] h-[480px]' : 'w-[320px] h-[240px]'}`}
          aria-label="Écran TI-Nspire CX II-T 320x240 RGB565 affichant N-Launcher"
        />
      </div>
    );
  }
);

function drawEllipsisText(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  text: string,
  maxWidth: number,
  color: string
) {
  ctx.fillStyle = color;
  if (ctx.measureText(text).width <= maxWidth) {
    ctx.fillText(text, x, y);
    return;
  }
  let s = text;
  while (s.length > 1 && ctx.measureText(s + '..').width > maxWidth) {
    s = s.slice(0, -1);
  }
  ctx.fillText(s + '..', x, y);
}

function drawScrollbarV(
  ctx: CanvasRenderingContext2D,
  y: number,
  h: number,
  scroll: number,
  vis: number,
  total: number,
  theme: ConsoleThemePalette
) {
  if (total <= vis || total <= 0) return;
  traceRoundedRect(ctx, 310, y, 4, h, 2);
  ctx.fillStyle = theme.pillBg;
  ctx.fill();

  const thumbH = Math.max(14, Math.round((h * vis) / total));
  const maxSc = total - vis;
  const thumbY = y + Math.round(((h - thumbH) * scroll) / Math.max(1, maxSc));
  traceRoundedRect(ctx, 310, thumbY, 4, thumbH, 2);
  ctx.fillStyle = theme.accentPrimary;
  ctx.fill();
}

function drawCircularAvatar(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  diam: number,
  avatarId: number,
  taken: boolean,
  theme: ConsoleThemePalette,
  imgCache: Map<string, HTMLImageElement>
) {
  const cx = x + diam / 2;
  const cy = y + diam / 2;
  const r = diam / 2;
  ctx.save();
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, Math.PI * 2);
  ctx.clip();

  const safeId = avatarId >= 0 && avatarId < NL_AVATAR_COUNT ? avatarId : 0;
  const url = getAvatarImageUrl(safeId);
  const img = imgCache.get(url);
  if (img && img.complete && img.naturalWidth > 0) {
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(img, x, y, diam, diam);
    ctx.imageSmoothingEnabled = false;
  } else {
    ctx.fillStyle = theme.pillBg;
    ctx.fillRect(x, y, diam, diam);
  }

  if (taken) {
    ctx.fillStyle = theme.isLight ? 'rgba(226, 232, 240, 0.62)' : 'rgba(15, 23, 42, 0.62)';
    ctx.fillRect(x, y, diam, diam);
  }
  ctx.restore();
}

function drawMulticolorFocusRing(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  diam: number,
  thickness: number,
  phaseRad: number,
  isLight: boolean,
  alpha: number
) {
  if (alpha <= 0.01) return;
  const palette = isLight
    ? ['#1d4ed8', '#7c3aed', '#be123c', '#d97706', '#047857', '#0f766e', '#1d4ed8']
    : ['#22d3ee', '#38bdf8', '#a855f7', '#ec4899', '#facc15', '#34d399', '#22d3ee'];

  const grad = ctx.createConicGradient(phaseRad, cx, cy);
  for (let i = 0; i < palette.length; i++) {
    grad.addColorStop(i / (palette.length - 1), palette[i]);
  }

  ctx.save();
  const rBase = diam / 2;
  // Subtle outer ambient aura
  ctx.globalAlpha = Math.max(0, Math.min(1, alpha * (isLight ? 0.24 : 0.32)));
  ctx.strokeStyle = grad;
  ctx.lineWidth = thickness + 4;
  ctx.beginPath();
  ctx.arc(cx, cy, rBase + thickness * 0.6 + 1, 0, Math.PI * 2);
  ctx.stroke();

  // Crisp luminous multicolor focus ring
  ctx.globalAlpha = Math.max(0, Math.min(1, alpha));
  ctx.strokeStyle = grad;
  ctx.lineWidth = thickness;
  ctx.beginPath();
  ctx.arc(cx, cy, rBase + thickness * 0.5, 0, Math.PI * 2);
  ctx.stroke();
  ctx.restore();
}

function profileSpringReboundScale(p: number): number {
  const c = Math.max(0, Math.min(1, p));
  if (c < 108 / 256) {
    const t = c / (108 / 256);
    const ease = 1 - Math.pow(1 - t, 3);
    return (288 / 256) * ease; // Overshoot to 1.125x
  }
  if (c < 152 / 256) {
    const t = (c - 108 / 256) / (44 / 256);
    return (288 - 40 * t) / 256; // Rebound dip to 0.968x
  }
  if (c < 192 / 256) {
    const t = (c - 152 / 256) / (40 / 256);
    return (248 + 8 * t) / 256; // Settle to 1.00x
  }
  return 1.0;
}

function drawProfileWaterDropWaves(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  progress: number,
  maxRadius: number,
  theme: ConsoleThemePalette
) {
  if (progress <= 0 || progress >= 1) return;
  const delays = [0, 26 / 256, 52 / 256];
  const lives = [156 / 256, 148 / 256, 140 / 256];

  ctx.save();
  ctx.beginPath();
  ctx.rect(0, NL_TOP_BAR_H, NL_SCREEN_W, NL_SCREEN_H - NL_TOP_BAR_H);
  ctx.clip();

  for (let w = 0; w < 3; w++) {
    const st = (progress - delays[w]) / lives[w];
    if (st <= 0 || st >= 1) continue;
    const ease = 1 - Math.pow(1 - st, 3);
    const rad = Math.max(6, 12 + ((maxRadius - w * 12) * ease));
    const env = st < 48 / 256 ? st / (48 / 256) : Math.pow((1 - st) / (208 / 256), 2);
    const peakAlpha = ((20 - w * 3) / 32) * env;
    if (peakAlpha <= 0.015) continue;

    const ringCol =
      w === 0
        ? theme.isLight
          ? theme.accentPrimary
          : '#ffffff'
        : w === 1
        ? theme.accentSecondary
        : theme.accentPrimary;

    ctx.save();
    ctx.globalAlpha = Math.min(1, peakAlpha);
    ctx.strokeStyle = ringCol;
    ctx.lineWidth = 3.2 - w * 0.4;
    ctx.beginPath();
    ctx.arc(cx, cy, rad, 0, Math.PI * 2);
    ctx.stroke();
    ctx.restore();
  }

  ctx.restore();
}

function drawPlusBadgeWithRotation(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  diam: number,
  rotProgress: number,
  alpha: number,
  full: boolean,
  focused: boolean,
  theme: ConsoleThemePalette
) {
  if (diam < 8 || alpha <= 0.01) return;
  ctx.save();
  ctx.globalAlpha *= Math.max(0, Math.min(1, alpha));

  const baseBg = theme.isLight ? theme.pillBg : '#242a36';
  const plusBg = focused ? lerpHexColor(baseBg, theme.accentPrimary, 0.22) : baseBg;
  const ringCol = focused ? theme.accentSecondary : theme.borderSubtle;
  const symCol = full
    ? theme.textSecondary
    : focused
    ? theme.isLight
      ? theme.accentPrimary
      : '#ffffff'
    : theme.textPrimary;

  ctx.beginPath();
  ctx.arc(cx, cy, diam / 2 + 1, 0, Math.PI * 2);
  ctx.fillStyle = ringCol;
  ctx.fill();

  ctx.beginPath();
  ctx.arc(cx, cy, diam / 2, 0, Math.PI * 2);
  ctx.fillStyle = plusBg;
  ctx.fill();

  ctx.translate(cx, cy);
  ctx.rotate(Math.max(0, Math.min(1, rotProgress)) * (Math.PI / 2));
  const armLen = Math.max(4, Math.round(diam * 0.42));
  const armThick = diam >= 46 ? 3 : 2;
  ctx.fillStyle = symCol;
  ctx.fillRect(-armThick / 2, -armLen / 2, armThick, armLen);
  ctx.fillRect(-armLen / 2, -armThick / 2, armLen, armThick);

  ctx.restore();
}

function drawProfileSwitchTransition(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette,
  imgCache: Map<string, HTMLImageElement>
) {
  const p = anim.profSwitchProgress;
  if (p >= 1 || p < 0) return;

  const origX = anim.profSwitchOriginX;
  const origY = anim.profSwitchOriginY;
  const origD = Math.max(16, anim.profSwitchOriginD);
  const tgtX = 160;
  const tgtY = 112;
  const tgtD = 76;

  const moveT = Math.min(1, p / (140 / 256));
  const moveEase = 1 - Math.pow(1 - moveT, 3);
  const curCx = origX + (tgtX - origX) * moveEase;
  const curCy = origY + (tgtY - origY) * moveEase;

  ctx.save();
  ctx.beginPath();
  ctx.rect(0, NL_TOP_BAR_H, NL_SCREEN_W, NL_SCREEN_H - NL_TOP_BAR_H);
  ctx.clip();

  /* Backdrop focus dim */
  const dimAlpha =
    p < 48 / 256
      ? (p / (48 / 256)) * 0.45
      : p < 184 / 256
      ? 0.45
      : ((1 - p) / (72 / 256)) * 0.45;
  if (dimAlpha > 0.01) {
    ctx.fillStyle = theme.isLight
      ? `rgba(185, 195, 210, ${dimAlpha})`
      : `rgba(4, 6, 12, ${dimAlpha})`;
    ctx.fillRect(0, NL_TOP_BAR_H, NL_SCREEN_W, NL_SCREEN_H - NL_TOP_BAR_H);
  }

  /* Stage 1: Water-drop luminous waves from exact icon origin + advancing center */
  drawProfileWaterDropWaves(ctx, origX, origY, p, 84, theme);
  if (p > 40 / 256) {
    drawProfileWaterDropWaves(ctx, curCx, curCy, p - 32 / 256, 96, theme);
  }

  const mainAlpha = p <= 192 / 256 ? 1 : Math.max(0, (1 - p) / (64 / 256));
  if (mainAlpha <= 0.01) {
    ctx.restore();
    return;
  }

  /* Stage 2: 4 Sequential Ghost Silhouettes trailing behind the zooming avatar */
  const ghostLags = [18 / 256, 36 / 256, 54 / 256, 72 / 256];
  const ghostAlphas = [19 / 32, 13 / 32, 9 / 32, 5 / 32];
  if (p < 188 / 256) {
    const ghostFade = p <= 132 / 256 ? 1 : Math.max(0, (188 / 256 - p) / (56 / 256));
    for (let g = 3; g >= 0; g--) {
      const gp = p - ghostLags[g];
      if (gp <= 0) continue;
      const gMoveT = Math.min(1, gp / (140 / 256));
      const gx = origX + (tgtX - origX) * gMoveT;
      const gy = origY + (tgtY - origY) * gMoveT + (g + 1) * 3;
      const gSpring = profileSpringReboundScale(gp);
      const gd = Math.max(14, origD + (tgtD - origD) * gSpring - (g + 1) * 5);
      const ga = ghostAlphas[g] * ghostFade;
      if (ga > 0.02) {
        ctx.save();
        ctx.globalAlpha = ga;
        drawCircularAvatar(
          ctx,
          gx - gd / 2,
          gy - gd / 2,
          gd,
          anim.profSwitchAvatarId,
          false,
          theme,
          imgCache
        );
        ctx.beginPath();
        ctx.arc(gx, gy, gd / 2, 0, Math.PI * 2);
        ctx.fillStyle = theme.accentPrimary;
        ctx.globalAlpha = ga * (0.12 + g * 0.05);
        ctx.fill();
        ctx.restore();
      }
    }
  }

  /* Stage 3 & 4: Main Avatar with Natural Spring Rebound + Luminous Halo + Fade Out */
  const springScale = profileSpringReboundScale(p);
  let mainD = origD + (tgtD - origD) * springScale;
  if (p > 192 / 256) {
    mainD += ((p - 192 / 256) / (64 / 256)) * 8;
  }

  drawMulticolorFocusRing(
    ctx,
    curCx,
    curCy,
    mainD + 2,
    3,
    anim.qpGlowPhase,
    theme.isLight,
    mainAlpha
  );
  ctx.save();
  ctx.globalAlpha = mainAlpha;
  drawCircularAvatar(
    ctx,
    curCx - mainD / 2,
    curCy - mainD / 2,
    mainD,
    anim.profSwitchAvatarId,
    false,
    theme,
    imgCache
  );

  /* Profile name pill beneath the rebounding avatar */
  const ap = getActiveProfile(s);
  if (ap && p >= 64 / 256 && mainAlpha >= 0.25) {
    ctx.font = 'bold 8px "JetBrains Mono", monospace';
    const tw = Math.ceil(ctx.measureText(ap.name).width);
    const lw = tw + 16;
    const px = curCx - lw / 2;
    const py = curCy + mainD / 2 + 8;
    if (py + 16 < NL_SCREEN_H) {
      traceRoundedRect(ctx, px - 1, py - 1, lw + 2, 16, 8);
      ctx.fillStyle = theme.accentPrimary;
      ctx.fill();
      traceRoundedRect(ctx, px, py, lw, 14, 7);
      ctx.fillStyle = theme.cardBgSel;
      ctx.fill();
      ctx.fillStyle = theme.textPrimary;
      ctx.textBaseline = 'middle';
      ctx.fillText(ap.name, curCx - tw / 2, py + 7.5);
    }
  }
  ctx.restore();
  ctx.restore();
}

function drawProfilePlusTransition(
  ctx: CanvasRenderingContext2D,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette
) {
  const p = anim.profPlusProgress;
  if (p >= 1 || p < 0) return;

  const origX = anim.profPlusOriginX;
  const origY = anim.profPlusOriginY;
  const tgtX = 160;
  const tgtY = 118;
  const origD = 34;
  const tgtD = 68;

  const moveT = Math.min(1, p / (144 / 256));
  const moveEase = 1 - Math.pow(1 - moveT, 3);
  const curCx = origX + (tgtX - origX) * moveEase;
  const curCy = origY + (tgtY - origY) * moveEase;

  drawProfileWaterDropWaves(ctx, origX, origY, p, 78, theme);

  const mainAlpha = p <= 176 / 256 ? 1 : Math.max(0, (1 - p) / (80 / 256));
  if (mainAlpha <= 0.01) return;

  /* 4 Sequential Ghost Silhouettes of the '+' button */
  const ghostLags = [20 / 256, 40 / 256, 60 / 256, 80 / 256];
  const ghostAlphas = [18 / 32, 13 / 32, 8 / 32, 4 / 32];
  for (let g = 3; g >= 0; g--) {
    const gp = p - ghostLags[g];
    if (gp <= 0) continue;
    const gMove = Math.min(1, gp / (144 / 256));
    const gx = origX + (tgtX - origX) * gMove;
    const gy = origY + (tgtY - origY) * gMove + (g + 1) * 2;
    const gSpring = profileSpringReboundScale(gp);
    const gd = Math.max(16, origD + (tgtD - origD) * gSpring - (g + 1) * 4);
    const ga = ghostAlphas[g] * mainAlpha;
    drawPlusBadgeWithRotation(ctx, gx, gy, gd, Math.min(1, gp), ga, false, true, theme);
  }

  /* Main '+' badge with spring rebound & 90 deg rotation */
  const springScale = profileSpringReboundScale(p);
  const mainD = origD + (tgtD - origD) * springScale;
  drawMulticolorFocusRing(
    ctx,
    curCx,
    curCy,
    mainD + 2,
    2.5,
    anim.qpGlowPhase,
    theme.isLight,
    mainAlpha
  );
  drawPlusBadgeWithRotation(ctx, curCx, curCy, mainD, Math.min(1, p), mainAlpha, false, true, theme);
}

function drawProfileBirthTransition(
  ctx: CanvasRenderingContext2D,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette,
  imgCache: Map<string, HTMLImageElement>,
  slotCx: number,
  slotCy: number
) {
  const p = anim.profBirthProgress;
  if (p >= 1 || p < 0) return;

  drawProfileWaterDropWaves(ctx, 160, 114, p, 82, theme);
  drawProfileWaterDropWaves(ctx, slotCx, slotCy, p, 52, theme);

  if (p < 192 / 256) {
    const moveT = p / (192 / 256);
    const ease = 1 - Math.pow(1 - moveT, 3);
    const curCx = 160 + (slotCx - 160) * ease;
    const curCy = 114 + (slotCy - 114) * ease;
    const curD = 64 + (38 - 64) * ease;
    const a = p <= 128 / 256 ? 1 : (192 / 256 - p) / (64 / 256);

    drawMulticolorFocusRing(ctx, curCx, curCy, curD + 2, 2.5, anim.qpGlowPhase, theme.isLight, a);
    ctx.save();
    ctx.globalAlpha = Math.max(0, Math.min(1, a));
    drawCircularAvatar(
      ctx,
      curCx - curD / 2,
      curCy - curD / 2,
      curD,
      anim.profBirthAvatarId,
      false,
      theme,
      imgCache
    );
    ctx.restore();
  }
}

function drawQuickProfileOverlay(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette,
  imgCache: Map<string, HTMLImageElement>
) {
  const m = Math.max(0, Math.min(1, anim.qpOpenAnim));
  if (m <= 0.005 && !s.sui.qpOpen) return;
  const ease = 1 - Math.pow(1 - m, 3);
  const dy = Math.round((1 - ease) * 12);
  const zoomInsetX = Math.round((1 - ease) * 14);
  const zoomInsetY = Math.round((1 - ease) * 6);

  ctx.save();
  ctx.fillStyle = theme.isLight
    ? `rgba(175, 185, 200, ${0.52 * ease})`
    : `rgba(5, 7, 12, ${0.58 * ease})`;
  ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);

  ctx.globalAlpha = Math.max(0.05, ease);
  const mx = 10 + zoomInsetX;
  const my = 52 + dy + zoomInsetY;
  const mw = 300 - zoomInsetX * 2;
  const mh = 136 - zoomInsetY * 2;
  const boxBg = theme.isLight ? '#fafcff' : theme.cardBg;

  traceRoundedRect(ctx, mx - 1, my - 1, mw + 2, mh + 2, 11);
  ctx.fillStyle = ease < 0.94 ? lerpHexColor(theme.accentPrimary, theme.borderSubtle, ease) : theme.borderSubtle;
  ctx.fill();
  traceRoundedRect(ctx, mx, my, mw, mh, 10);
  ctx.fillStyle = boxBg;
  ctx.fill();

  if (ease < 0.98) {
    const sweepW = Math.round(mw * ease);
    ctx.fillStyle = theme.accentPrimary;
    ctx.fillRect(160 - Math.floor(sweepW / 2), my, sweepW, 2);
  }

  ctx.textBaseline = 'middle';
  ctx.font = 'bold 8px "JetBrains Mono", monospace';
  ctx.fillStyle = theme.textPrimary;
  ctx.fillText('PROFILS', mx + 14, my + 16);

  const pcount = Math.max(1, s.profiles.items.length);
  const cntStr = `${pcount}/${NL_MAX_PROFILES}`;
  ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
  const cw = Math.ceil(ctx.measureText(cntStr).width) + 10;
  traceRoundedRect(ctx, mx + mw - 12 - cw, my + 9, cw, 14, 4);
  ctx.fillStyle = theme.pillBg;
  ctx.fill();
  ctx.fillStyle = theme.accentSecondary;
  ctx.fillText(cntStr, mx + mw - 7 - cw, my + 16.5);

  const totalSlots = pcount + 1;
  const visMax = 6;
  const diam = 34;
  const stride = 44;
  const cur = Math.min(totalSlots - 1, Math.max(0, s.sui.qpCursor));
  const shown = Math.min(visMax, totalSlots);
  let winStart = 0;
  if (totalSlots > visMax) {
    winStart = cur - Math.floor(visMax / 2);
    if (winStart < 0) winStart = 0;
    if (winStart > totalSlots - visMax) winStart = totalSlots - visMax;
  }
  const stripW = shown * diam + (shown - 1) * (stride - diam);
  const startX = 160 - Math.floor(stripW / 2);
  const cy = 114 + dy;

  const qpMouse = isMouseModeActive(s) && s.sui.hoverValid;
  ctx.font = 'bold 8px "JetBrains Mono", monospace';
  if (winStart > 0) {
    ctx.fillStyle = qpMouse && s.sui.hoverKind === 'QP_PREV' ? theme.accentPrimary : theme.textSecondary;
    ctx.fillText('<', mx + 6, cy);
  }
  if (winStart + shown < totalSlots) {
    ctx.fillStyle = qpMouse && s.sui.hoverKind === 'QP_NEXT' ? theme.accentPrimary : theme.textSecondary;
    ctx.fillText('>', mx + mw - 12, cy);
  }

  const rel = cur - winStart;
  const tgtGx = startX + rel * stride + diam / 2;
  const glowCx = anim.qpGlowX > 0 ? anim.qpGlowX : tgtGx;
  const trailCx = anim.qpTrailX > 0 ? anim.qpTrailX : glowCx;
  const glowCy = (anim.qpGlowY > 0 ? anim.qpGlowY : 114) + dy;

  /* Diffused ghost glow wake when navigating between profiles */
  if (Math.abs(glowCx - trailCx) >= 3) {
    const midWx = (glowCx + trailCx) * 0.5;
    drawMulticolorFocusRing(ctx, trailCx, glowCy, 38, 2, anim.qpGlowPhase - 0.5, theme.isLight, ease * 0.32);
    drawMulticolorFocusRing(ctx, midWx, glowCy, 40, 2, anim.qpGlowPhase - 0.25, theme.isLight, ease * 0.5);
  }

  const bouncePx = anim.qpSelBounce * 4;
  drawMulticolorFocusRing(ctx, glowCx, glowCy, 36 + bouncePx, 3, anim.qpGlowPhase, theme.isLight, ease);

  let birthSlotCx = 160;
  for (let v = 0; v < shown; v++) {
    const slot = winStart + v;
    const scx = startX + v * stride + Math.floor(diam / 2);
    if (slot === anim.profBirthSlot) birthSlotCx = scx;
    const focused = slot === cur;
    const slotHov = qpMouse && s.sui.hoverKind === 'QP_SLOT' && s.sui.hoverIndex === slot;

    /* Staggered sequential cascade reveal on selector open */
    const stagDelay = v * 0.085;
    const slotOpen = m <= stagDelay ? 0.12 : Math.min(1, (m - stagDelay) / (1 - stagDelay));
    const stagDy = Math.round((1 - slotOpen) * 10);
    const slotAlpha = Math.max(0.18, slotOpen * ease);

    const slotScale = slot >= 0 && slot < 9 ? anim.qpSlotScales[slot] : focused ? 1.06 : 0.88;
    let curDiam = diam * slotScale + (focused ? bouncePx : 0);
    curDiam = Math.max(16, curDiam * (0.62 + 0.38 * slotOpen));

    const liftY = focused && anim.qpSelBounce > 0.05 ? -1 : 0;
    const scy = cy + stagDy + liftY;
    const sx = scx - curDiam / 2;
    const sy = scy - curDiam / 2;

    if (focused) {
      traceCapsule(ctx, sx + 2, sy + curDiam - 1, curDiam - 4, 4);
      ctx.fillStyle = theme.isLight ? 'rgba(195, 205, 220, 0.85)' : 'rgba(4, 6, 10, 0.85)';
      ctx.fill();
    }

    if (slot < pcount) {
      const p = s.profiles.items[slot];
      const isAct = slot === s.profiles.activeIndex;
      ctx.save();
      ctx.globalAlpha = slotAlpha;
      if (!focused) {
        ctx.beginPath();
        ctx.arc(scx, scy, curDiam / 2 + (slotHov ? 2 : 1), 0, Math.PI * 2);
        ctx.fillStyle = slotHov ? theme.accentSecondary : isAct ? theme.accentPrimary : theme.borderSubtle;
        ctx.fill();
      }
      drawCircularAvatar(ctx, sx, sy, curDiam, p.avatarId, false, theme, imgCache);
      ctx.restore();

      const shortLbl = p.name.slice(0, 7);
      ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
      const lw = ctx.measureText(shortLbl).width;
      ctx.fillStyle = focused || slotHov ? theme.textPrimary : theme.textSecondary;
      ctx.fillText(shortLbl, Math.round(scx - lw / 2), cy + Math.floor(diam / 2) + 10);

      if (isAct) {
        traceRoundedRect(ctx, scx - 14, sy - 11, 28, 9, 4.5);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();
        ctx.font = 'bold 6.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textOnAccent;
        ctx.fillText('ACT.', scx - 11, sy - 6.3);
      }
    } else {
      const full = pcount >= NL_MAX_PROFILES || firstFreeAvatar(s) < 0;
      const rotProgress = focused ? anim.qpPlusFocus : 0;
      drawPlusBadgeWithRotation(ctx, scx, scy, curDiam, rotProgress, slotAlpha, full, focused, theme);

      const addLbl = full ? 'Complet' : 'Ajouter';
      ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
      const lw = ctx.measureText(addLbl).width;
      ctx.fillStyle = focused ? theme.textPrimary : theme.textSecondary;
      ctx.fillText(addLbl, Math.round(scx - lw / 2), cy + Math.floor(diam / 2) + 10);
    }
  }

  ctx.fillStyle = theme.borderSubtle;
  ctx.fillRect(mx + 12, my + mh - 24, mw - 24, 1);
  const hint = '< > Choisir   EXE Valider   ESC Fermer';
  ctx.font = 'bold 7px "JetBrains Mono", monospace';
  const hw = ctx.measureText(hint).width;
  ctx.fillStyle = theme.textSecondary;
  ctx.fillText(hint, Math.round(mx + (mw - hw) / 2), my + mh - 12);

  if (anim.profBirthProgress < 1) {
    drawProfileBirthTransition(ctx, anim, theme, imgCache, birthSlotCx, cy - 2);
  }

  ctx.restore();
}

const SETTINGS_ICONS_11X11: Record<SettingsIconId, number[]> = {
  POWER: [0x020, 0x020, 0x1ac, 0x326, 0x222, 0x222, 0x202, 0x306, 0x18c, 0x0f8, 0x000],
  SCREEN: [0x000, 0x3fe, 0x202, 0x2e2, 0x2a2, 0x202, 0x202, 0x3fe, 0x070, 0x1fc, 0x000],
  TOUCHPAD: [0x1fc, 0x202, 0x222, 0x272, 0x222, 0x2f2, 0x222, 0x3fe, 0x222, 0x222, 0x1fc],
  LAUNCH_KEY: [0x1fc, 0x202, 0x262, 0x272, 0x27a, 0x272, 0x262, 0x202, 0x3fe, 0x3fe, 0x000],
  KEYBOARD: [0x000, 0x3fe, 0x202, 0x2aa, 0x202, 0x2aa, 0x202, 0x272, 0x202, 0x3fe, 0x000],
  SORT: [0x000, 0x080, 0x09e, 0x080, 0x09c, 0x080, 0x298, 0x1c0, 0x090, 0x000, 0x000],
  STATS: [0x000, 0x070, 0x070, 0x076, 0x076, 0x376, 0x376, 0x376, 0x376, 0x7ff, 0x000],
  SEARCH: [0x000, 0x1e0, 0x210, 0x210, 0x210, 0x210, 0x1e8, 0x00c, 0x006, 0x002, 0x000],
  RESET: [0x000, 0x178, 0x384, 0x1e2, 0x002, 0x002, 0x202, 0x104, 0x0f8, 0x000, 0x000],
  INFO: [0x0f8, 0x104, 0x222, 0x202, 0x262, 0x222, 0x222, 0x272, 0x104, 0x0f8, 0x000],
  CREDITS: [0x020, 0x070, 0x070, 0x1fc, 0x3fe, 0x1fc, 0x0f8, 0x1dc, 0x104, 0x000, 0x000],
  GRID: [0x000, 0x3de, 0x3de, 0x3de, 0x3de, 0x000, 0x3de, 0x3de, 0x3de, 0x3de, 0x000],
  TAG: [0x000, 0x1fc, 0x104, 0x104, 0x104, 0x1fc, 0x000, 0x3fe, 0x000, 0x0f8, 0x000],
  BOTTOMBAR: [0x000, 0x3fe, 0x202, 0x202, 0x202, 0x202, 0x3fe, 0x2aa, 0x3fe, 0x000, 0x000],
  BATTERY: [0x000, 0x000, 0x3fc, 0x204, 0x2d7, 0x2d7, 0x2d7, 0x204, 0x3fc, 0x000, 0x000],
  REFRESH: [0x000, 0x0f8, 0x106, 0x20e, 0x200, 0x000, 0x002, 0x382, 0x304, 0x0f8, 0x000],
  CAPTURE: [0x000, 0x070, 0x3fe, 0x202, 0x272, 0x28a, 0x28a, 0x272, 0x202, 0x3fe, 0x000],
  ICONS: [0x000, 0x3fe, 0x202, 0x2c2, 0x2c2, 0x212, 0x23a, 0x27e, 0x2fe, 0x3fe, 0x000],
  HOTKEY: [0x000, 0x060, 0x0c0, 0x180, 0x3fe, 0x030, 0x060, 0x0c0, 0x180, 0x000, 0x000],
  PROFILES: [0x000, 0x0e0, 0x1f0, 0x1f0, 0x0e0, 0x000, 0x1f0, 0x3f8, 0x3f8, 0x3f8, 0x000],
};

function drawSettingsIcon11x11(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  iconId: SettingsIconId,
  color: string
) {
  const rows = SETTINGS_ICONS_11X11[iconId];
  if (!rows) return;
  ctx.fillStyle = color;
  for (let r = 0; r < 11; r++) {
    const bits = rows[r];
    for (let c = 0; c < 11; c++) {
      if ((bits >> (10 - c)) & 1) {
        ctx.fillRect(x + c, y + r, 1, 1);
      }
    }
  }
}

function drawSettingsIconBadge(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  size: number,
  iconId: SettingsIconId,
  rowT: number,
  isDanger: boolean,
  theme: ConsoleThemePalette
) {
  const targetBg = isDanger ? '#dc2626' : theme.accentPrimary;
  const targetFg = isDanger ? '#ffffff' : theme.textOnAccent;
  const baseBg = theme.isLight ? theme.bgMain : theme.pillBg;
  const baseFg = isDanger ? '#f87171' : theme.accentSecondary;

  const badgeBg = lerpHexColor(baseBg, targetBg, rowT);
  const iconFg = lerpHexColor(baseFg, targetFg, rowT);

  traceRoundedRect(ctx, x, y, size, size, 5);
  ctx.fillStyle = badgeBg;
  ctx.fill();

  const gx = x + Math.floor((size - 11) / 2);
  const gy = y + Math.floor((size - 11) / 2);
  drawSettingsIcon11x11(ctx, gx, gy, iconId, iconFg);
}

function drawSettingsChrome(
  ctx: CanvasRenderingContext2D,
  theme: ConsoleThemePalette,
  title: string,
  headerIcon: SettingsIconId | null,
  hints: [string, string][],
  gearRotationRad = 0
) {
  ctx.fillStyle = theme.bgMain;
  ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);

  ctx.fillStyle = theme.bgTopBar;
  ctx.fillRect(0, 0, NL_SCREEN_W, NL_TOP_BAR_H);
  ctx.fillStyle = theme.borderSubtle;
  ctx.fillRect(0, NL_TOP_BAR_H, NL_SCREEN_W, 1);

  if (headerIcon) {
    drawSettingsIconBadge(
      ctx,
      10,
      4,
      17,
      headerIcon,
      0.75,
      headerIcon === 'RESET',
      theme
    );
  } else {
    drawLauncherEmblem(ctx, 10, 5, 15, theme);
  }

  ctx.fillStyle = theme.textPrimary;
  ctx.font = 'bold 8.5px "JetBrains Mono", monospace';
  ctx.textBaseline = 'middle';
  ctx.fillText(title, 33, 13);

  /* Discreet Power button right next to the Settings title (triggered by DEL or click) */
  const titleW = Math.ceil(ctx.measureText(title).width);
  const pwrX = Math.min(222, 33 + titleW + 6);
  if (pwrX + 17 <= 248) {
    drawSettingsIconBadge(ctx, pwrX, 4, 17, 'POWER', 0.65, false, theme);
    if (pwrX + 41 <= 248) {
      traceRoundedRect(ctx, pwrX + 19, 6, 22, 13, 3);
      ctx.fillStyle = theme.pillBg;
      ctx.fill();
      ctx.font = 'bold 6.8px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('DEL', pwrX + 22, 12.8);
    }
  }

  drawGearButton(
    ctx,
    NL_GEAR_BTN_X,
    NL_GEAR_BTN_Y,
    NL_GEAR_BTN_D,
    theme,
    true,
    gearRotationRad
  );

  ctx.fillStyle = theme.borderSubtle;
  ctx.fillRect(0, NL_BOTTOM_BAR_Y - 1, NL_SCREEN_W, 1);
  ctx.fillStyle = theme.bgBottomBar;
  ctx.fillRect(0, NL_BOTTOM_BAR_Y, NL_SCREEN_W, NL_BOTTOM_BAR_H);

  ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
  ctx.textBaseline = 'middle';
  const widths = hints.map(([k, l]) => ctx.measureText(k + ' ' + l).width);
  const gap = 14;
  const totalW = widths.reduce((a, b) => a + b, 0) + gap * Math.max(0, hints.length - 1);
  let sx = Math.round((NL_SCREEN_W - totalW) / 2);
  const sy = NL_BOTTOM_BAR_Y + 10;

  hints.forEach(([k, l], idx) => {
    ctx.fillStyle = theme.accentSecondary;
    ctx.fillText(k, sx, sy);
    const kw = ctx.measureText(k).width;
    ctx.fillStyle = theme.textSecondary;
    ctx.fillText(' ' + l, sx + kw, sy);
    sx += widths[idx] + gap;
  });
}

function drawMiniGridPreviewSingle(
  ctx: CanvasRenderingContext2D,
  px: number,
  py: number,
  pw: number,
  ph: number,
  layout: LayoutMode,
  nameT: number,
  scT: number,
  theme: ConsoleThemePalette
) {
  const m = getLayoutMetrics(layout, nameT >= 0.5, scT >= 0.5);
  const footerReserve = Math.round(3 + 7 * scT);
  const innerX = px + 10;
  const innerY = py + 5;
  const innerW = pw - 20;
  const innerH = ph - 9 - footerReserve;

  const gapX = m.cols > 1 ? 5 : 0;
  const gapY = m.rows > 1 ? 4 : 0;
  const maxCw = Math.floor((innerW - gapX * (m.cols - 1)) / m.cols);
  const maxCh = Math.floor((innerH - gapY * (m.rows - 1)) / m.rows);
  const nameDeduct = Math.round(3 * nameT);
  const side = Math.max(4, Math.min(maxCw, maxCh - nameDeduct));
  const gridW = m.cols * side + (m.cols - 1) * gapX;
  const gridH = m.rows * (side + nameDeduct) + (m.rows - 1) * gapY;
  const startX = innerX + Math.floor((innerW - gridW) / 2);
  const startY = innerY + Math.floor((innerH - gridH) / 2);

  for (let r = 0; r < m.rows; r++) {
    for (let c = 0; c < m.cols; c++) {
      const cx = startX + c * (side + gapX);
      const cy = startY + r * (side + nameDeduct + gapY);
      const first = r === 0 && c === 0;
      traceRoundedRect(ctx, cx, cy, side, side, side >= 14 ? 3 : 2);
      ctx.fillStyle = first ? theme.accentPrimary : theme.cardBgSel;
      ctx.fill();
      if (nameT > 0.05 && side >= 6) {
        const lw = side > 4 ? side - 4 : side;
        const baseCol = first ? theme.textPrimary : theme.textSecondary;
        ctx.fillStyle = lerpHexColor(theme.bgTopBar, baseCol, nameT);
        ctx.fillRect(cx + Math.floor((side - lw) / 2), cy + side + 1, lw, 2);
      }
    }
  }
}

function drawMiniGridPreview(
  ctx: CanvasRenderingContext2D,
  px: number,
  py: number,
  pw: number,
  ph: number,
  layoutFrom: LayoutMode,
  layoutTo: LayoutMode,
  layoutTransT: number,
  nameT: number,
  scT: number,
  theme: ConsoleThemePalette
) {
  traceRoundedRect(ctx, px, py, pw, ph, 7);
  ctx.fillStyle = theme.borderSubtle;
  ctx.fill();
  traceRoundedRect(ctx, px + 1, py + 1, pw - 2, ph - 2, 6);
  ctx.fillStyle = theme.bgTopBar;
  ctx.fill();

  if (layoutTransT < 0.99 && layoutFrom !== layoutTo) {
    const ease = 1 - Math.pow(1 - Math.max(0, Math.min(1, layoutTransT)), 3);
    ctx.save();
    ctx.globalAlpha = 1 - ease;
    drawMiniGridPreviewSingle(ctx, px, py, pw, ph, layoutFrom, nameT, scT, theme);
    ctx.globalAlpha = ease;
    drawMiniGridPreviewSingle(ctx, px, py, pw, ph, layoutTo, nameT, scT, theme);
    ctx.restore();
  } else {
    drawMiniGridPreviewSingle(ctx, px, py, pw, ph, layoutTo, nameT, scT, theme);
  }

  if (scT > 0.05) {
    const barY = py + ph - 7 + Math.round((1 - scT) * 5);
    if (barY < py + ph - 1) {
      ctx.save();
      ctx.globalAlpha = scT;
      ctx.fillStyle = theme.borderSubtle;
      ctx.fillRect(px + 4, barY - 1, pw - 8, 1);
      const pillW = 24;
      const pillGap = 8;
      const startX = px + Math.floor((pw - (4 * pillW + 3 * pillGap)) / 2);
      for (let i = 0; i < 4; i++) {
        const sx = startX + i * (pillW + pillGap);
        ctx.fillStyle = theme.accentSecondary;
        ctx.fillRect(sx, barY + 2, 8, 2);
        ctx.fillStyle = theme.textSecondary;
        ctx.fillRect(sx + 10, barY + 2, 12, 2);
      }
      ctx.restore();
    }
  }
}

type SettingsAnimState = {
  switchHotkeyAnim: number;
  switchShowNamesAnim: number;
  switchShortcutsAnim: number;
  switchTouchpadAnim: number;
  switchTouchpadModeAnim: number;
  tpTabTransAnim: number;
  tpStyleGlowAnim: number;
  tpStyleHaloPhase: number;
  curStylePhase: number;
  lastTpStyleCursor: number;
  curX: number;
  curY: number;
  curVx: number;
  curVy: number;
  curOpacity: number;
  curIdleTicks: number;
  curPosInit: boolean;
  curTrailX: number[];
  curTrailY: number[];
  curTrailAlpha: number[];
  tpSliderPos: number;
  tpSliderVel: number;
  tpDetentPulse: number;
  tpNotchLast: number;
  tpSliderDragging: boolean;
  tpSliderDragPos: number;
  tpSpeedSliderPos: number;
  tpSpeedSliderVel: number;
  tpSpeedDetentPulse: number;
  tpSpeedNotchLast: number;
  tpSpeedSliderDragging: boolean;
  tpSpeedSliderDragPos: number;
  setOpenProgress: number;
  lastUiScreen: SettingsScreenId;
  prevUiScreen: SettingsScreenId;
  secTransProgress: number;
  secTransDir: 1 | -1;
  selY: number;
  targetSelY: number;
  selGlowProgress: number;
  prevCursor: number;
  lastCursorKey: string;
  listScrollPx: number;
  targetScrollPx: number;
  modalProgress: number;
  modalBtnProgress: number;
  lastModalOpen: boolean;
  layoutFrom: LayoutMode;
  lastLayout: LayoutMode;
  layoutTransProgress: number;
  valPulseProgress: number;
  valPulseDir: 1 | -1;
  lastCategory: CategoryId;
  catIndX: number;
  catIndW: number;
  catTransProgress: number;
  catTransDir: 1 | -1;
  searchBarAnim: number;
  scKeyX: number;
  scKeyY: number;
  scKeyW: number;
  lastScPickKey: number;
  setDropCanvas: HTMLCanvasElement | null;
  setDropImage: ImageData | null;
  qpOpenAnim: number;
  qpGlowX: number;
  qpGlowY: number;
  qpTrailX: number;
  qpSelBounce: number;
  qpPlusFocus: number;
  qpSlotScales: number[];
  lastQpCursor: number;
  qpGlowPhase: number;
  lastProfSwitchSeq: number;
  profSwitchProgress: number;
  profSwitchAvatarId: number;
  profSwitchOriginX: number;
  profSwitchOriginY: number;
  profSwitchOriginD: number;
  lastProfPlusSeq: number;
  profPlusProgress: number;
  profPlusOriginX: number;
  profPlusOriginY: number;
  lastProfBirthSeq: number;
  profBirthProgress: number;
  profBirthAvatarId: number;
  profBirthSlot: number;
};

function getScreenChromeMeta(screen: SettingsScreenId): {
  title: string;
  icon: SettingsIconId | null;
  hints: [string, string][];
} {
  switch (screen) {
    case 'SETTINGS_HOTKEY':
      return {
        title: 'RACCOURCI GLOBAL TI-OS',
        icon: 'HOTKEY',
        hints: [
          ['EXE', 'Activer/Des.'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_DISPLAY':
      return {
        title: 'AFFICHAGE',
        icon: 'SCREEN',
        hints: [
          ['UP/DN', 'Option'],
          ['EXE', 'Modifier'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_TOUCHPAD':
      return {
        title: 'NAVIGATION TACTILE',
        icon: 'TOUCHPAD',
        hints: [
          ['UP/DN', 'Option'],
          ['L/R', 'Regler'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_SHORTCUTS':
      return {
        title: 'TOUCHES DE LANCEMENT',
        icon: 'LAUNCH_KEY',
        hints: [
          ['EXE', 'Choisir'],
          ['DEL', 'Effacer'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_KEY_HELP':
      return {
        title: 'COMMANDES DU LAUNCHER',
        icon: 'KEYBOARD',
        hints: [
          ['UP/DN', 'Defiler'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_SORT':
      return {
        title: 'TRI DE LA BIBLIOTHEQUE',
        icon: 'SORT',
        hints: [
          ['UP/DN', 'Choisir'],
          ['EXE', 'Appliquer'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_STATS':
      return {
        title: 'STATISTIQUES DE JEU',
        icon: 'STATS',
        hints: [
          ['UP/DN', 'Parcourir'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_CAPTURE_BATCH':
      return {
        title: "CAPTURES D'ECRAN",
        icon: 'CAPTURE',
        hints: [
          ['DROITE', 'Passer'],
          ['ESC', 'Annuler'],
        ],
      };
    case 'SETTINGS_ICONS':
      return {
        title: 'GESTION DES ICONES',
        icon: 'ICONS',
        hints: [
          ['FLECHES', 'Nav.'],
          ['EXE', 'Supprimer'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_PROFILES':
      return {
        title: 'PROFILS UTILISATEURS',
        icon: 'PROFILES',
        hints: [
          ['UP/DN', 'Nav.'],
          ['EXE', 'Ouvrir'],
          ['ESC', 'Retour'],
        ],
      };
    case 'SETTINGS_RESET':
      return {
        title: 'REINITIALISER LES REGLAGES',
        icon: 'RESET',
        hints: [
          ['L/R', 'Choisir'],
          ['EXE', 'Valider'],
          ['ESC', 'Annuler'],
        ],
      };
    case 'SETTINGS_INFO':
      return {
        title: 'INFORMATIONS SYSTEME',
        icon: 'INFO',
        hints: [['EXE / ESC', 'Retour']],
      };
    case 'SETTINGS_CREDITS':
      return {
        title: 'A PROPOS ET CREDITS',
        icon: 'CREDITS',
        hints: [['EXE / ESC', 'Retour']],
      };
    default:
      return {
        title: 'PARAMETRES',
        icon: null,
        hints: [
          ['FLECHES', 'Nav.'],
          ['EXE', 'Ouvrir'],
          ['ESC', 'Fermer'],
        ],
      };
  }
}

/* Exact 28x28 alpha masks extracted from icons8-select-cursor-24.png (hotspot 6,2) and icons8-select-48.png (hotspot 13,13) */
const G_CURSOR_ARROW_28 = new Uint8Array([
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 16, 76, 95, 28,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 39,184,201,130, 48,  6,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 54,252,255,231,143, 57, 11,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,241,160, 61, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,247,181, 60,  6,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,254,247,202, 75,  6,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,254,246,209,100, 20,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,251,209,117, 34,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,212,127, 45,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,220,137, 49,  4,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,229,151, 46,  6,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,255,240,178, 40,  6,  1,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,255,254,244,202, 55,  9,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,255,255,254,246,201, 80, 17,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,249,196, 96, 25,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,252,181, 71,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,255,255,255,255,255,255,254,252,240,228,181, 78,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,255,254,246,236,228,221,214,205,193,168,141, 82, 27,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,253,244,199,156,122, 90, 57, 27,  3,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,255,255,235,150, 29, 13,  9,  7,  4,  2,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,255,253,208,110, 31,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 55,255,255,255,247,186, 96, 28,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 54,252,255,237,168, 82, 20,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 42,199,211,154, 62, 10,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 18, 84,105, 30,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
]);

const G_CURSOR_CIRCLE_28 = new Uint8Array([
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 23,104,164,193,193,164,104, 22,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  3, 80,183,248,255,255,255,255,255,255,248,182, 79,  3,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0, 21,165,255,244,199,112, 42,  8,  8, 42,113,199,244,255,164, 21,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  8,219,255,211, 29,  3,  0,  0,  0,  0,  0,  0,  3, 29,212,255,218,  8,  0,  0,  0,  0,  0,
    0,  0,  0,  0, 21,219,255,131,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,131,255,218, 21,  0,  0,  0,  0,
    0,  0,  0,  3,165,255,131,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,131,255,163,  3,  0,  0,  0,
    0,  0,  0, 80,255,211,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,213,255, 78,  0,  0,  0,
    0,  0,  0,183,244, 29,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 29,245,182,  0,  0,  0,
    0,  0, 23,248,199,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,200,248, 22,  0,  0,
    0,  0,105,255,112,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,114,255,102,  0,  0,
    0,  0,164,255, 42,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 43,255,163,  0,  0,
    0,  0,192,255,  8,  0,  0,  0,  0,  0,  0,  0,  0,219,219,  0,  0,  0,  0,  0,  0,  0,  0,  9,255,191,  0,  0,
    0,  0,192,255,  8,  0,  0,  0,  0,  0,  0,  0,  0,219,218,  0,  0,  0,  0,  0,  0,  0,  0,  9,255,191,  0,  0,
    0,  0,164,255, 42,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 44,255,163,  0,  0,
    0,  0,104,255,112,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,115,255,102,  0,  0,
    0,  0, 22,248,199,  3,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  3,200,248, 21,  0,  0,
    0,  0,  0,182,244, 29,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 30,245,181,  0,  0,  0,
    0,  0,  0, 79,255,212,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,214,255, 77,  0,  0,  0,
    0,  0,  0,  3,164,255,131,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,132,255,161,  3,  0,  0,  0,
    0,  0,  0,  0, 21,218,255,131,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,132,255,217, 20,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  8,218,255,213, 29,  3,  0,  0,  0,  0,  0,  0,  3, 30,214,255,217,  8,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0, 21,162,255,245,200,114, 43,  9,  9, 43,115,200,245,255,161, 20,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  3, 78,182,248,255,255,255,255,255,255,248,181, 77,  3,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 22,103,163,192,192,162,102, 22,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
]);

/* Exact 22x28 alpha & fill masks extracted from Dark_Blue_Gradient_Animated_Cursor--pointer--SweezyCursors.png (hotspot 7,1) */
const G_CURSOR_POINTER_ALPHA_28 = new Uint8Array([
    0,  0,  0,  0,  0, 98,227,253,210, 71,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0, 63,240,252,253,253,221, 30,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,135,251,254,255,253,253, 93,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,102,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,102,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,102,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,102,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,102,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,102,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,255,157, 34,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,140,250,255,255,255,253,253,235,122,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0, 78,162,151,185,250,255,255,255,253,253,253,252,197,182,125, 25,  0,  0,  0,  0,  0,
   94,242,254,254,255,250,255,255,255,255,255,255,253,252,252,254,198, 52,  0,  0,  0,  0,
  213,253,255,254,253,250,255,255,255,255,255,255,255,255,255,254,254,247,219,138, 23,  0,
  254,254,255,255,253,250,255,255,255,255,255,255,255,255,255,255,254,249,252,253,201, 19,
  249,253,255,255,252,249,255,255,255,255,255,255,255,255,255,255,255,255,255,253,254,116,
  217,253,255,255,255,252,255,255,255,255,255,255,255,255,255,255,255,255,255,255,252,181,
  159,253,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,212,
   84,253,253,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,217,
    0,206,253,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,253,200,
    0,117,254,253,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,254,156,  0,
    0,204,253,254,255,255,255,255,255,255,255,255,255,255,255,255,255,253,252, 78,  0,  0,
   66,237,252,254,255,255,255,255,255,255,255,255,255,255,255,255,255,253,195,  0,  0,  0,
    0,125,254,253,255,255,255,255,255,255,255,255,255,255,255,255,253,253, 89,  0,  0,  0,
    0,  0,153,252,253,255,255,255,255,255,255,255,255,255,254,254,253,145,  0,  0,  0,  0,
    0,  0,  0,147,254,253,253,255,255,255,255,255,255,254,253,254,158,  0,  0,  0,  0,  0,
    0,  0,  0,  0,103,215,254,253,253,254,255,252,253,253,225,116,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0, 23,107,177,226,254,255,242,197,126, 33,  0,  0,  0,  0,  0,  0,  0,
]);

const G_CURSOR_POINTER_FILL_28 = new Uint8Array([
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,204,208,197,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,202,202,195,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,197,197,190,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,192,192,185,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,187,187,181,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,181,181,175,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,176,176,170,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,171,171,165,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,166,166,160,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,161,161,159,152,153,145,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,156,156,156,156,156,154,142,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,149,147,  0,  0,150,150,150,150,150,150,150,150,150,142,  0,  0,  0,  0,  0,  0,
    0,138,145,145,135,  0,145,145,145,145,145,145,145,145,145,141,133,136,  0,  0,  0,  0,
    0,133,139,139,133,  0,139,139,139,139,139,139,139,139,139,139,139,139,136,  0,  0,  0,
    0,127,134,134,132,124,134,134,134,134,134,134,134,134,134,134,134,134,134,  0,  0,  0,
    0,  0,128,129,129,129,129,129,129,129,129,129,129,129,129,129,129,129,125,  0,  0,  0,
    0,  0,120,124,124,124,124,124,124,124,124,124,124,124,124,124,124,124,124,  0,  0,  0,
    0,  0,  0,118,119,119,119,119,119,119,119,119,119,119,119,119,119,119,119,  0,  0,  0,
    0,  0,  0,  0,111,114,114,114,114,114,114,114,114,114,114,114,114,114,113,  0,  0,  0,
    0,  0,  0,  0,  0,108,108,108,108,108,108,108,108,108,108,108,108,108,106,  0,  0,  0,
    0,  0,  0,  0,  0,101,103,103,103,103,103,103,103,103,103,103,103,102,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0, 95, 98, 98, 98, 98, 98, 98, 98, 98, 98, 95,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0, 92, 92, 93, 93, 93, 93, 93, 93, 93, 91,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0, 87, 88, 88, 88, 88, 88, 88, 87,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0, 84, 82, 82, 84,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
]);

function drawCursorAlphaSprite(
  ctx: CanvasRenderingContext2D,
  style: CursorStyle,
  hx: number,
  hy: number,
  opacity: number,
  theme: ConsoleThemePalette,
  animPhase = 0
) {
  if (opacity <= 0.01 || style === 'none') return;
  const isPointer = style === 'pointer';
  const mask =
    style === 'arrow'
      ? G_CURSOR_ARROW_28
      : style === 'circle'
      ? G_CURSOR_CIRCLE_28
      : G_CURSOR_POINTER_ALPHA_28;
  const fillMask = isPointer ? G_CURSOR_POINTER_FILL_28 : null;
  const hotX = style === 'arrow' ? 6 : style === 'circle' ? 13 : 7;
  const hotY = style === 'arrow' ? 2 : style === 'circle' ? 13 : 1;
  const sw = isPointer ? 22 : 28;
  const sh = 28;
  const sx = Math.round(hx - hotX);
  const sy = Math.round(hy - hotY);

  const haloCol = theme.isLight ? '#0f172a' : '#ffffff';
  const coreHi = lerpHexColor(theme.accentSecondary, '#ffffff', theme.isLight ? 0.18 : 0.34);
  const coreLo = theme.accentPrimary;
  const ptrEdge = '#141821';
  const ptrTop = lerpHexColor('#08c6e0', theme.accentSecondary, 10 / 32);
  const ptrBot = lerpHexColor('#142a56', theme.accentPrimary, 10 / 32);

  ctx.save();
  for (let dy = -1; dy <= sh; dy++) {
    const py = sy + dy;
    if (py < 0 || py >= NL_SCREEN_H) continue;
    for (let dx = -1; dx <= sw; dx++) {
      const px = sx + dx;
      if (px < 0 || px >= NL_SCREEN_W) continue;
      const a8 = dx >= 0 && dx < sw && dy >= 0 && dy < sh ? mask[dy * sw + dx] : 0;

      if (a8 < 180) {
        let maxNb = 0;
        for (let ny = dy - 1; ny <= dy + 1; ny++) {
          if (ny < 0 || ny >= sh) continue;
          for (let nx = dx - 1; nx <= dx + 1; nx++) {
            if (nx < 0 || nx >= sw) continue;
            const na = mask[ny * sw + nx];
            if (na > maxNb) maxNb = na;
          }
        }
        if (maxNb > 48) {
          const strength = isPointer ? (theme.isLight ? 0.18 : 0.31) : 0.56;
          const maxHalo = isPointer ? 0.34 : 0.62;
          const haloA = Math.min(maxHalo, ((maxNb - (a8 >> 1)) / 255) * opacity * strength);
          if (haloA > 0.02) {
            ctx.globalAlpha = haloA;
            ctx.fillStyle = haloCol;
            ctx.fillRect(px, py, 1, 1);
          }
        }
      }

      if (a8 === 0) continue;
      let fillStyle: string;
      if (isPointer && fillMask) {
        const f8 = fillMask[dy * sw + dx];
        if (f8 === 0) {
          fillStyle = ptrEdge;
        } else {
          const wave = Math.sin(dy * 0.22 + dx * 0.12 + animPhase);
          const gradT = Math.max(0, Math.min(1, dy / sh + wave * 0.22));
          const gradCol = lerpHexColor(ptrTop, ptrBot, gradT);
          fillStyle = f8 >= 248 ? gradCol : lerpHexColor(ptrEdge, gradCol, f8 / 255);
        }
      } else {
        const distHot = Math.abs(dx - hotX) + Math.abs(dy - hotY);
        const gradT = Math.min(1, distHot / 28);
        fillStyle = lerpHexColor(coreHi, coreLo, gradT);
      }

      ctx.globalAlpha = (a8 / 255) * opacity;
      ctx.fillStyle = fillStyle;
      ctx.fillRect(px, py, 1, 1);
    }
  }
  ctx.restore();
}

function drawTouchpadGraphicalCursor(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette
) {
  const vis = isTouchpadCursorVisible(s);
  if (!vis) return;
  const effOp = Math.max(0.44, Math.min(1, anim.curOpacity > 0.02 ? anim.curOpacity : 1));
  const style: CursorStyle =
    s.settings.touchpadCursorStyle === 'circle' || s.settings.touchpadCursorStyle === 'pointer'
      ? s.settings.touchpadCursorStyle
      : 'arrow';

  const hx = anim.curPosInit ? anim.curX : computeTouchpadCursorTarget(s).x;
  const hy = anim.curPosInit ? anim.curY : computeTouchpadCursorTarget(s).y;

  const trailCore = lerpHexColor(theme.accentSecondary, '#ffffff', theme.isLight ? 0.12 : 0.3);
  const trailGlow = theme.accentPrimary;

  ctx.save();
  for (let i = 4; i >= 0; i--) {
    const ta = anim.curTrailAlpha[i] ?? 0;
    if (ta <= 0.02) continue;
    const tx = anim.curTrailX[i] ?? hx;
    const ty = anim.curTrailY[i] ?? hy;
    if (Math.abs(tx - hx) <= 1 && Math.abs(ty - hy) <= 1) continue;

    if (i <= 1) {
      const ghostOp = ta * effOp * (i === 0 ? 0.26 : 0.14);
      if (ghostOp >= 0.04) {
        drawCursorAlphaSprite(ctx, style, tx, ty, ghostOp, theme, anim.curStylePhase);
      }
    }

    const rad = i === 0 ? 4 : i <= 2 ? 3 : 2;
    ctx.globalAlpha = Math.min(0.6, ta * effOp * 0.55);
    ctx.fillStyle = i <= 1 ? trailCore : trailGlow;
    ctx.beginPath();
    ctx.arc(tx, ty, rad, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();

  drawCursorAlphaSprite(ctx, style, hx, hy, effOp, theme, anim.curStylePhase);
}

function drawSingleSettingsSectionBody(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  screen: SettingsScreenId,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette,
  isActiveScreen: boolean
) {
  const sui = s.sui;
  ctx.textBaseline = 'middle';
  const selGlow = isActiveScreen ? anim.selGlowProgress : 1;

  switch (screen) {
    case 'SETTINGS_MAIN': {
      const vis = 6;
      const rowY0 = 32;
      const rowH = 28;
      const rowGap = 2;
      const stride = rowH + rowGap;
      const scrollPx = isActiveScreen ? Math.round(anim.listScrollPx) : sui.menuScroll * stride;
      const selY = isActiveScreen
        ? Math.round(anim.selY - anim.listScrollPx)
        : rowY0 + (sui.menuCursor - sui.menuScroll) * stride;

      ctx.save();
      ctx.beginPath();
      ctx.rect(0, rowY0 - 2, NL_SCREEN_W, vis * stride - rowGap + 4);
      ctx.clip();

      if (SETTINGS_MENU_ITEMS.length > 0) {
        traceRoundedRect(ctx, 11, selY - 1, 294, rowH + 2, 7);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();
      }

      for (let idx = 0; idx < SETTINGS_MENU_ITEMS.length; idx++) {
        const item = SETTINGS_MENU_ITEMS[idx];
        const ry = rowY0 + idx * stride - scrollPx;
        if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
        const sel = idx === sui.menuCursor;
        let rowT = sel ? selGlow : 0;
        if (!sel && isActiveScreen && idx === anim.prevCursor && selGlow < 1) {
          rowT = (1 - selGlow) * 0.5;
        }
        const isDanger = item.id === 'SETTINGS_RESET';

        traceRoundedRect(ctx, 12, ry, 292, rowH, 6);
        ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, rowT);
        ctx.fill();

        drawSettingsIconBadge(ctx, 17, ry + 5, 18, item.icon, rowT, isDanger, theme);

        let badge = '';
        if (item.id === 'SETTINGS_REFRESH_LIB') {
          badge = s.sys.cacheValid ? 'CACHE OK' : 'SCAN';
        } else if (item.id === 'SETTINGS_HOTKEY') badge = s.settings.globalHotkey ? 'ON' : 'OFF';
        else if (item.id === 'SETTINGS_DISPLAY') badge = LAYOUT_LABELS[s.settings.layout];
        else if (item.id === 'SETTINGS_TOUCHPAD') badge = s.settings.touchpadNav ? 'ON' : 'OFF';
        else if (item.id === 'SETTINGS_SHORTCUTS') {
          const assigned = s.games.filter((g) => Boolean(g.shortcutKey)).length;
          if (assigned > 0) badge = String(assigned);
        } else if (item.id === 'SETTINGS_SORT') {
          badge =
            s.settings.sortOrder === 'alpha_asc'
              ? 'A-Z'
              : s.settings.sortOrder === 'alpha_desc'
              ? 'Z-A'
              : s.settings.sortOrder === 'recent_add'
              ? 'RECENT'
              : s.settings.sortOrder === 'oldest_add'
              ? 'ANCIEN'
              : s.settings.sortOrder === 'last_used'
              ? 'JOUE'
              : s.settings.sortOrder === 'most_launched'
              ? 'TOP'
              : 'TEMPS';
        } else if (item.id === 'SETTINGS_CAPTURE_BATCH') {
          const scrCount = s.games.filter((g) => g.hasScreenshot && Boolean(g.screenshotDataUrl)).length;
          badge = `${scrCount}/${s.games.length}`;
        } else if (item.id === 'SETTINGS_ICONS') {
          const icoCount = s.games.filter(
            (g) => g.iconSource !== 'NONE' && Boolean(g.capturedIconDataUrl)
          ).length;
          badge = String(icoCount);
        } else if (item.id === 'SETTINGS_PROFILES') {
          badge = getActiveProfile(s).name.slice(0, 9);
        } else if (item.id === 'SETTINGS_INFO') badge = `v${NL_LAUNCHER_VERSION_STR}`;

        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        const titleCol =
          isDanger && sel
            ? lerpHexColor(theme.textPrimary, '#f87171', selGlow)
            : theme.textPrimary;
        const maxTextW = badge ? 186 : 236;
        drawEllipsisText(ctx, 41, ry + 9.5, item.label, maxTextW, titleCol);

        ctx.font = '6.8px "JetBrains Mono", monospace';
        const subCol = lerpHexColor(
          theme.textSecondary,
          isDanger ? '#f87171' : theme.accentSecondary,
          rowT * 0.55
        );
        drawEllipsisText(ctx, 41, ry + 20, item.subtitle, maxTextW, subCol);

        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        if (badge) {
          const bw = Math.ceil(ctx.measureText(badge).width);
          const pillW = bw + 8;
          const pillX = 286 - pillW;
          traceRoundedRect(ctx, pillX, ry + 7, pillW, 14, 4);
          ctx.fillStyle = lerpHexColor(theme.pillBg, theme.bgTopBar, rowT * 0.5);
          ctx.fill();
          ctx.fillStyle = lerpHexColor(theme.textSecondary, theme.accentSecondary, rowT);
          ctx.fillText(badge, pillX + 4, ry + 14.5);
        }
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = lerpHexColor(theme.textSecondary, theme.accentPrimary, rowT);
        ctx.fillText('>', 291, ry + 14.5);
      }
      ctx.restore();

      drawScrollbarV(
        ctx,
        rowY0,
        vis * stride - rowGap,
        scrollPx,
        vis * stride,
        SETTINGS_MENU_ITEMS.length * stride,
        theme
      );
      break;
    }

    case 'SETTINGS_HOTKEY': {
      traceRoundedRect(ctx, 14, 34, 292, 54, 7);
      ctx.fillStyle = lerpHexColor(theme.borderSubtle, theme.accentPrimary, selGlow);
      ctx.fill();
      traceRoundedRect(ctx, 15, 35, 290, 52, 6);
      ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, selGlow * 0.45);
      ctx.fill();

      drawSettingsIconBadge(
        ctx,
        22,
        49,
        22,
        'HOTKEY',
        anim.switchHotkeyAnim,
        false,
        theme
      );

      ctx.font = 'bold 8px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textPrimary;
      ctx.fillText('Raccourci Ctrl + Menu (TI-OS)', 50, 50);

      ctx.font = '7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = lerpHexColor(
        theme.textSecondary,
        theme.accentSecondary,
        anim.switchHotkeyAnim
      );
      ctx.fillText(
        s.settings.globalHotkey
          ? 'Actif dans les menus TI-OS'
          : 'Desactive (recommande par defaut)',
        50,
        65
      );
      drawToggleSwitch(ctx, 262, 52, anim.switchHotkeyAnim, theme);

      traceRoundedRect(ctx, 14, 96, 292, 114, 7);
      ctx.fillStyle = theme.cardBg;
      ctx.fill();

      ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Module :', 24, 111);
      ctx.fillStyle = s.sys.hotkeyActive ? '#4ade80' : theme.textSecondary;
      ctx.fillText(
        s.sys.hotkeyActive
          ? 'nl_hotkey.tns installe (startup)'
          : 'Non installe (aucun hook)',
        96,
        111
      );

      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Touche :', 24, 127);
      ctx.fillStyle = theme.accentSecondary;
      ctx.fillText('Ctrl + Menu (depuis TI-OS)', 96, 127);

      ctx.fillStyle = theme.textPrimary;
      ctx.fillText('Protections actives :', 24, 145);
      ctx.font = '7.2px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('- Ignore pendant les jeux (verrou actif).', 24, 159);
      ctx.fillText('- Anti-rebond + garde anti-reentrance.', 24, 172);
      ctx.fillText('- Maintenir ESC ou ON au boot coupe le hook.', 24, 185);
      if (s.sys.hotkeyDiag) {
        drawEllipsisText(ctx, 24, 199, s.sys.hotkeyDiag, 270, theme.accentSecondary);
      }
      break;
    }

    case 'SETTINGS_DISPLAY': {
      const dispIcons: SettingsIconId[] = [
        'GRID',
        'TAG',
        'BOTTOMBAR',
        'POWER',
      ];
      const vis = 3;
      const rowY0 = 30;
      const rowH = 28;
      const rowGap = 2;
      const stride = rowH + rowGap;
      const scrollPx = isActiveScreen
        ? Math.round(anim.listScrollPx)
        : sui.displayScroll * stride;
      const selY = isActiveScreen
        ? Math.round(anim.selY - anim.listScrollPx)
        : rowY0 + (sui.displayCursor - sui.displayScroll) * stride;

      ctx.save();
      ctx.beginPath();
      ctx.rect(0, rowY0 - 2, NL_SCREEN_W, vis * stride - rowGap + 4);
      ctx.clip();

      traceRoundedRect(ctx, 11, selY - 1, 294, rowH + 2, 7);
      ctx.fillStyle = theme.accentPrimary;
      ctx.fill();

      for (let idx = 0; idx < NL_DISPLAY_OPT_COUNT; idx++) {
        const ry = rowY0 + idx * stride - scrollPx;
        if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
        const sel = idx === sui.displayCursor;
        let rowT = sel ? selGlow : 0;
        if (!sel && isActiveScreen && idx === anim.prevCursor && selGlow < 1) {
          rowT = (1 - selGlow) * 0.5;
        }

        traceRoundedRect(ctx, 12, ry, 292, rowH, 6);
        ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, rowT);
        ctx.fill();

        drawSettingsIconBadge(ctx, 17, ry + 5, 18, dispIcons[idx], rowT, false, theme);

        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;

        if (idx === 0) {
          ctx.fillText('Grille des jaquettes', 41, ry + 14.5);
          const layLabel = `< ${LAYOUT_LABELS[s.settings.layout]} >`;
          const lw = ctx.measureText(layLabel).width;
          const pulseShift = Math.round(anim.valPulseDir * anim.valPulseProgress * 2);
          const pillX = 284 - lw - 10 + pulseShift;
          if (anim.valPulseProgress > 0.05) {
            traceRoundedRect(ctx, pillX - 1, ry + 5, lw + 14, 18, 5);
            ctx.fillStyle = lerpHexColor(theme.pillBg, theme.accentPrimary, anim.valPulseProgress);
            ctx.fill();
          }
          traceRoundedRect(ctx, pillX, ry + 6, lw + 12, 16, 4);
          ctx.fillStyle = lerpHexColor(
            theme.pillBg,
            theme.cardBgSel,
            anim.valPulseProgress * 0.5
          );
          ctx.fill();
          ctx.fillStyle = lerpHexColor(
            theme.accentSecondary,
            theme.accentPrimary,
            anim.valPulseProgress
          );
          ctx.fillText(layLabel, pillX + 6, ry + 14.5);
        } else if (idx === 1) {
          ctx.fillText('Titres sous les jaquettes', 41, ry + 14.5);
          drawToggleSwitch(ctx, 260, ry + 7, anim.switchShowNamesAnim, theme);
        } else if (idx === 2) {
          ctx.fillText("Barre d'aide en bas", 41, ry + 14.5);
          drawToggleSwitch(ctx, 260, ry + 7, anim.switchShortcutsAnim, theme);
        } else if (idx === 3) {
          ctx.fillText('Eteindre la calculatrice', 41, ry + 14.5);
          const pwrStr = 'DEL/EXE';
          const pw = ctx.measureText(pwrStr).width;
          const pillW = pw + 12;
          const pillX = 294 - pillW;
          traceRoundedRect(ctx, pillX, ry + 6, pillW, 16, 4);
          ctx.fillStyle = sel ? theme.accentPrimary : theme.pillBg;
          ctx.fill();
          ctx.fillStyle = sel ? theme.textOnAccent : theme.accentSecondary;
          ctx.fillText(pwrStr, pillX + 6, ry + 14.5);
        }
      }
      ctx.restore();

      drawScrollbarV(
        ctx,
        rowY0,
        vis * stride - rowGap,
        scrollPx,
        vis * stride,
        NL_DISPLAY_OPT_COUNT * stride,
        theme
      );

      ctx.font = '7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Apercu en direct :', 16, 128);
      drawMiniGridPreview(
        ctx,
        56,
        136,
        208,
        78,
        anim.layoutFrom,
        s.settings.layout,
        anim.layoutTransProgress,
        anim.switchShowNamesAnim,
        anim.switchShortcutsAnim,
        theme
      );
      break;
    }

    case 'SETTINGS_TOUCHPAD': {
      const isTab1 = sui.tpTab === 1;
      const mouseOn = isActiveScreen && isMouseModeActive(s) && sui.hoverValid;

      /* Top 2-Tab Bar (y = 28..44) */
      traceRoundedRect(ctx, 14, 28, 292, 16, 5);
      ctx.fillStyle = theme.pillBg;
      ctx.fill();

      const t0Bg = !isTab1 ? theme.accentPrimary : theme.pillBg;
      const t0Fg = !isTab1 ? theme.textOnAccent : theme.textSecondary;
      const t1Bg = isTab1 ? theme.accentPrimary : theme.pillBg;
      const t1Fg = isTab1 ? theme.textOnAccent : theme.accentSecondary;

      traceRoundedRect(ctx, 15, 29, 138, 14, 4);
      ctx.fillStyle = t0Bg;
      ctx.fill();
      if (mouseOn && sui.hoverKind === 'SET_TAB' && sui.hoverIndex === 0) {
        ctx.strokeStyle = theme.accentSecondary;
        ctx.lineWidth = 1;
        ctx.stroke();
      }
      ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
      ctx.fillStyle = t0Fg;
      ctx.fillText('1. Navigation tactile', 27, 36.3);

      traceRoundedRect(ctx, 155, 29, 150, 14, 4);
      ctx.fillStyle = t1Bg;
      ctx.fill();
      if (mouseOn && sui.hoverKind === 'SET_TAB' && sui.hoverIndex === 1) {
        ctx.strokeStyle = theme.accentSecondary;
        ctx.lineWidth = 1;
        ctx.stroke();
      }
      ctx.fillStyle = t1Fg;
      ctx.fillText('2. Apparence curseur', 163, 36.3);

      if (!isTab1) {
        const curRow = Math.max(0, Math.min(4, sui.tpCursor));
        const sens = clampTouchpadSensitivity(s.settings.touchpadSensitivity);
        const cspd = clampCursorSpeed(s.settings.cursorSpeed);
        const isMouseMode = s.settings.touchpadMode === 'mouse';

        const hovR0 = mouseOn && sui.hoverKind === 'SET_ROW' && sui.hoverIndex === 0;
        const hovR1 = mouseOn && sui.hoverKind === 'SET_ROW' && sui.hoverIndex === 1;
        const hovR2 =
          mouseOn &&
          (sui.hoverKind === 'SET_ROW' || sui.hoverKind === 'SET_SLIDER_NOTCH') &&
          sui.hoverIndex === 2;
        const hovR3 =
          mouseOn &&
          (sui.hoverKind === 'SET_ROW' || sui.hoverKind === 'SET_SLIDER_NOTCH') &&
          sui.hoverIndex === 3;
        const hovR4 = mouseOn && sui.hoverKind === 'SET_ROW' && sui.hoverIndex === 4;

        /* Row 0 (y=47..75, h=28): Navigation Mode Selector (Pave tactile / Souris) */
        traceRoundedRect(ctx, 14, 47, 292, 28, 6);
        ctx.fillStyle =
          curRow === 0
            ? lerpHexColor(theme.borderSubtle, theme.accentPrimary, selGlow)
            : hovR0
            ? theme.accentSecondary
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 15, 48, 290, 26, 5);
        ctx.fillStyle =
          curRow === 0 ? lerpHexColor(theme.cardBg, theme.cardBgSel, selGlow * 0.45) : theme.cardBg;
        ctx.fill();

        ctx.font = 'bold 7.6px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText('Mode de navigation', 22, 61);

        const modeTxt = `< ${TOUCHPAD_MODE_LABELS[s.settings.touchpadMode]} >`;
        const mw = Math.ceil(ctx.measureText(modeTxt).width) + 12;
        const mxPill = 294 - mw;
        traceRoundedRect(ctx, mxPill, 53, mw, 16, 4);
        ctx.fillStyle = isMouseMode ? theme.accentPrimary : theme.pillBg;
        ctx.fill();
        ctx.fillStyle = isMouseMode ? theme.textOnAccent : theme.accentSecondary;
        ctx.fillText(modeTxt, mxPill + 6, 61.3);

        /* Row 1 (y=77..104, h=27): Touchpad Slide Nav ON/OFF (Mode 1) */
        traceRoundedRect(ctx, 14, 77, 292, 27, 6);
        ctx.fillStyle =
          curRow === 1
            ? lerpHexColor(theme.borderSubtle, theme.accentPrimary, selGlow)
            : hovR1
            ? theme.accentSecondary
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 15, 78, 290, 25, 5);
        ctx.fillStyle =
          curRow === 1 ? lerpHexColor(theme.cardBg, theme.cardBgSel, selGlow * 0.45) : theme.cardBg;
        ctx.fill();

        ctx.font = 'bold 7.4px "JetBrains Mono", monospace';
        ctx.fillStyle = isMouseMode ? theme.textSecondary : theme.textPrimary;
        ctx.fillText('Glissement au pave (Mode 1)', 22, 90.5);
        drawToggleSwitch(ctx, 262, 83, anim.switchTouchpadAnim, theme);

        /* Row 2 (y=106..144, h=38): 5-Detent Slide Sensitivity (Mode 1) */
        const row2Bg =
          curRow === 2 ? lerpHexColor(theme.cardBg, theme.cardBgSel, selGlow * 0.45) : theme.cardBg;
        traceRoundedRect(ctx, 14, 106, 292, 38, 6);
        ctx.fillStyle =
          curRow === 2
            ? lerpHexColor(theme.borderSubtle, theme.accentPrimary, selGlow)
            : hovR2
            ? theme.accentSecondary
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 15, 107, 290, 36, 5);
        ctx.fillStyle = row2Bg;
        ctx.fill();

        ctx.font = 'bold 7.4px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText('Sensibilite pave (5 crans)', 22, 115);

        const pulse = Math.max(0, Math.min(1, anim.tpDetentPulse));
        const sensLabel = TP_SENS_LABELS[sens] ?? TP_SENS_LABELS[NL_TP_SENS_DEFAULT];
        ctx.font = 'bold 7px "JetBrains Mono", monospace';
        const lblW = Math.ceil(ctx.measureText(sensLabel).width) + 10;
        const lblX = 294 - lblW;
        traceRoundedRect(ctx, lblX, 109, lblW, 12, 4);
        ctx.fillStyle =
          curRow === 2
            ? lerpHexColor(theme.pillBg, theme.accentPrimary, 0.38 + pulse * 0.45)
            : theme.pillBg;
        ctx.fill();
        ctx.fillStyle =
          curRow === 2
            ? lerpHexColor(theme.accentSecondary, theme.textOnAccent, 0.44)
            : theme.accentSecondary;
        ctx.fillText(sensLabel, lblX + 5, 115.2);

        const trackX = 34;
        const trackW = 252;
        const trackY = 128;
        const posFp = anim.tpSliderDragging ? anim.tpSliderDragPos : anim.tpSliderPos;
        const handleX = Math.max(
          trackX - 3,
          Math.min(trackX + trackW + 3, trackX + (posFp * trackW) / 1024)
        );
        const fillW = Math.max(0, Math.min(trackW, handleX - trackX));
        const railCol = lerpHexColor(theme.dotInactive, theme.borderSubtle, 0.32);
        const activeRailCol =
          !isMouseMode && s.settings.touchpadNav
            ? theme.accentPrimary
            : lerpHexColor(theme.dotInactive, theme.accentPrimary, 0.62);

        traceRoundedRect(ctx, trackX, trackY - 2, trackW, 4, 2);
        ctx.fillStyle = railCol;
        ctx.fill();
        if (fillW >= 4) {
          traceRoundedRect(ctx, trackX, trackY - 2, fillW, 4, 2);
          ctx.fillStyle = activeRailCol;
          ctx.fill();
        }

        for (let n = 0; n < NL_TP_SENS_COUNT; n++) {
          const nx = trackX + n * 63;
          const passed = n <= sens;
          const isCur = n === sens;
          const hovN =
            mouseOn &&
            sui.hoverKind === 'SET_SLIDER_NOTCH' &&
            sui.hoverIndex === 2 &&
            sui.hoverSub === n;

          if ((isCur && pulse > 0.03) || hovN) {
            ctx.save();
            ctx.globalAlpha = hovN ? 0.5 : pulse * 0.45;
            ctx.fillStyle = theme.accentSecondary;
            ctx.beginPath();
            ctx.arc(nx, trackY, 5.5 + (hovN ? 1.5 : pulse * 2), 0, Math.PI * 2);
            ctx.fill();
            ctx.restore();
          }

          const tickCol = isCur
            ? theme.accentSecondary
            : passed
            ? activeRailCol
            : lerpHexColor(theme.textSecondary, theme.dotInactive, 0.5);
          ctx.fillStyle = tickCol;
          ctx.beginPath();
          ctx.arc(nx, trackY, 3.5, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = row2Bg;
          ctx.beginPath();
          ctx.arc(nx, trackY, 1.5, 0, Math.PI * 2);
          ctx.fill();

          ctx.font = 'bold 6.5px "JetBrains Mono", monospace';
          ctx.fillStyle = isCur || hovN ? theme.accentSecondary : theme.textSecondary;
          ctx.fillText(String(n + 1), nx - 2, 138);
        }

        const expand = pulse > 0.45 ? 1 : 0;
        const kw = 10 + expand * 2;
        const kh = 11 + expand * 2;
        const kx = Math.round(handleX - kw / 2);
        const ky = Math.round(trackY - kh / 2);
        traceRoundedRect(ctx, kx - 1, ky - 1, kw + 2, kh + 2, 3);
        ctx.fillStyle =
          curRow === 2 || pulse > 0
            ? lerpHexColor(theme.accentPrimary, theme.accentSecondary, 0.5 + pulse * 0.45)
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, kx, ky, kw, kh, 3);
        ctx.fillStyle = '#f8fafc';
        ctx.fill();
        ctx.fillStyle = activeRailCol;
        ctx.fillRect(Math.round(handleX - 1), ky + 2, 2, kh - 4);

        /* Row 3 (y=146..184, h=38): 5-Detent Mouse Pointer Speed Slider (Mode 2) */
        const row3Bg =
          curRow === 3 ? lerpHexColor(theme.cardBg, theme.cardBgSel, selGlow * 0.45) : theme.cardBg;
        traceRoundedRect(ctx, 14, 146, 292, 38, 6);
        ctx.fillStyle =
          curRow === 3
            ? lerpHexColor(theme.borderSubtle, theme.accentPrimary, selGlow)
            : hovR3
            ? theme.accentSecondary
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 15, 147, 290, 36, 5);
        ctx.fillStyle = row3Bg;
        ctx.fill();

        ctx.font = 'bold 7.4px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText('Vitesse souris (5 crans)', 22, 155);

        const spdPulse = Math.max(0, Math.min(1, anim.tpSpeedDetentPulse));
        const spdLabel = CURSOR_SPEED_LABELS[cspd] ?? CURSOR_SPEED_LABELS[NL_CURSOR_SPEED_DEFAULT];
        ctx.font = 'bold 7px "JetBrains Mono", monospace';
        const slblW = Math.ceil(ctx.measureText(spdLabel).width) + 10;
        const slblX = 294 - slblW;
        traceRoundedRect(ctx, slblX, 149, slblW, 12, 4);
        ctx.fillStyle =
          curRow === 3
            ? lerpHexColor(theme.pillBg, theme.accentPrimary, 0.38 + spdPulse * 0.45)
            : theme.pillBg;
        ctx.fill();
        ctx.fillStyle =
          curRow === 3
            ? lerpHexColor(theme.accentSecondary, theme.textOnAccent, 0.44)
            : theme.accentSecondary;
        ctx.fillText(spdLabel, slblX + 5, 155.2);

        const strackY = 168;
        const sposFp = anim.tpSpeedSliderDragging ? anim.tpSpeedSliderDragPos : anim.tpSpeedSliderPos;
        const shandleX = Math.max(
          trackX - 3,
          Math.min(trackX + trackW + 3, trackX + (sposFp * trackW) / 1024)
        );
        const sfillW = Math.max(0, Math.min(trackW, shandleX - trackX));
        const sactiveRail = isMouseMode
          ? theme.accentPrimary
          : lerpHexColor(theme.dotInactive, theme.accentPrimary, 0.62);

        traceRoundedRect(ctx, trackX, strackY - 2, trackW, 4, 2);
        ctx.fillStyle = railCol;
        ctx.fill();
        if (sfillW >= 4) {
          traceRoundedRect(ctx, trackX, strackY - 2, sfillW, 4, 2);
          ctx.fillStyle = sactiveRail;
          ctx.fill();
        }

        for (let n = 0; n < NL_CURSOR_SPEED_COUNT; n++) {
          const nx = trackX + n * 63;
          const passed = n <= cspd;
          const isCur = n === cspd;
          const hovN =
            mouseOn &&
            sui.hoverKind === 'SET_SLIDER_NOTCH' &&
            sui.hoverIndex === 3 &&
            sui.hoverSub === n;

          if ((isCur && spdPulse > 0.03) || hovN) {
            ctx.save();
            ctx.globalAlpha = hovN ? 0.5 : spdPulse * 0.45;
            ctx.fillStyle = theme.accentSecondary;
            ctx.beginPath();
            ctx.arc(nx, strackY, 5.5 + (hovN ? 1.5 : spdPulse * 2), 0, Math.PI * 2);
            ctx.fill();
            ctx.restore();
          }

          const tickCol = isCur
            ? theme.accentSecondary
            : passed
            ? sactiveRail
            : lerpHexColor(theme.textSecondary, theme.dotInactive, 0.5);
          ctx.fillStyle = tickCol;
          ctx.beginPath();
          ctx.arc(nx, strackY, 3.5, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = row3Bg;
          ctx.beginPath();
          ctx.arc(nx, strackY, 1.5, 0, Math.PI * 2);
          ctx.fill();

          ctx.font = 'bold 6.5px "JetBrains Mono", monospace';
          ctx.fillStyle = isCur || hovN ? theme.accentSecondary : theme.textSecondary;
          ctx.fillText(String(n + 1), nx - 2, 178);
        }

        const skExp = spdPulse > 0.45 ? 1 : 0;
        const skw = 10 + skExp * 2;
        const skh = 11 + skExp * 2;
        const skx = Math.round(shandleX - skw / 2);
        const sky = Math.round(strackY - skh / 2);
        traceRoundedRect(ctx, skx - 1, sky - 1, skw + 2, skh + 2, 3);
        ctx.fillStyle =
          curRow === 3 || spdPulse > 0
            ? lerpHexColor(theme.accentPrimary, theme.accentSecondary, 0.5 + spdPulse * 0.45)
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, skx, sky, skw, skh, 3);
        ctx.fillStyle = '#f8fafc';
        ctx.fill();
        ctx.fillStyle = sactiveRail;
        ctx.fillRect(Math.round(shandleX - 1), sky + 2, 2, skh - 4);

        /* Row 4 (y=186..217, h=31): Open "Apparence du curseur" Tab Card */
        traceRoundedRect(ctx, 14, 186, 292, 31, 6);
        ctx.fillStyle =
          curRow === 4
            ? lerpHexColor(theme.borderSubtle, theme.accentPrimary, selGlow)
            : hovR4
            ? theme.accentSecondary
            : theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 15, 187, 290, 29, 5);
        ctx.fillStyle =
          curRow === 4 ? lerpHexColor(theme.cardBg, theme.cardBgSel, selGlow * 0.45) : theme.cardBg;
        ctx.fill();

        ctx.font = 'bold 7.6px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText('Apparence du curseur', 22, 195);
        ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.accentSecondary;
        const activeStyleLbl =
          CURSOR_STYLE_LABELS[clampCursorStyleToCard(s.settings.touchpadCursorStyle)];
        ctx.fillText(`Style actif : ${activeStyleLbl}`, 22, 207.5);

        traceRoundedRect(ctx, 232, 194, 64, 15, 4);
        ctx.fillStyle = curRow === 4 || hovR4 ? theme.accentPrimary : theme.pillBg;
        ctx.fill();
        ctx.font = 'bold 7px "JetBrains Mono", monospace';
        ctx.fillStyle = curRow === 4 || hovR4 ? theme.textOnAccent : theme.accentSecondary;
        ctx.fillText('TAB / EXE', 238, 201.8);
      } else {
        /* TAB 1: "Apparence du curseur" — 3 Authentic Cursor Vignette Cards + Live Preview */
        const cardStyles: CursorStyle[] = ['arrow', 'circle', 'pointer'];
        const cardTitles = ['Fleche', 'Cercle', 'Pointeur'];
        const cardSubs = ['Pointe exacte', 'Centre exact', 'Main animee'];
        const selCard = Math.max(0, Math.min(2, sui.tpStyleCursor));
        const styleGlow = Math.max(0, Math.min(1, anim.tpStyleGlowAnim));

        const cardW = 90;
        const cardH = 116;
        const gapX = 11;
        const startX = 14;
        const cardY = 50;

        for (let c = 0; c < 3; c++) {
          const cstyle = cardStyles[c];
          const cx = startX + c * (cardW + gapX);
          const isFocused = c === selCard;
          const isSaved = clampCursorStyleToCard(s.settings.touchpadCursorStyle) === cstyle;
          const isHovered = mouseOn && sui.hoverKind === 'SET_STYLE_CARD' && sui.hoverIndex === c;

          if (isFocused || isHovered) {
            const haloWave = 0.5 + 0.5 * Math.sin(anim.tpStyleHaloPhase);
            const haloCol = lerpHexColor(theme.bgMain, theme.accentSecondary, 0.35 + haloWave * 0.55);
            traceRoundedRect(ctx, cx - 2, cardY - 2, cardW + 4, cardH + 4, 9);
            ctx.fillStyle = haloCol;
            ctx.fill();
          }

          traceRoundedRect(ctx, cx, cardY, cardW, cardH, 7);
          ctx.fillStyle = isFocused
            ? lerpHexColor(theme.borderSubtle, theme.accentPrimary, styleGlow)
            : isHovered
            ? theme.accentSecondary
            : isSaved
            ? theme.accentPrimary
            : theme.borderSubtle;
          ctx.fill();
          traceRoundedRect(ctx, cx + 1, cardY + 1, cardW - 2, cardH - 2, 6);
          ctx.fillStyle =
            isFocused || isHovered
              ? lerpHexColor(theme.cardBg, theme.cardBgSel, styleGlow * 0.45)
              : theme.cardBg;
          ctx.fill();

          if (isSaved) {
            traceRoundedRect(ctx, cx + cardW - 38, cardY + 5, 33, 11, 3);
            ctx.fillStyle = theme.accentPrimary;
            ctx.fill();
            ctx.font = 'bold 6.5px "JetBrains Mono", monospace';
            ctx.fillStyle = theme.textOnAccent;
            ctx.fillText('ACTIF', cx + cardW - 35, cardY + 10.7);
          }

          const stageX = cx + Math.floor((cardW - 62) / 2);
          const stageY = cardY + 20;
          const stageBg = theme.isLight ? '#e2e8f0' : '#0c111c';
          traceRoundedRect(ctx, stageX, stageY, 62, 52, 6);
          ctx.fillStyle = theme.borderSubtle;
          ctx.fill();
          traceRoundedRect(ctx, stageX + 1, stageY + 1, 60, 50, 5);
          ctx.fillStyle = stageBg;
          ctx.fill();

          const midX = stageX + 31;
          const midY = stageY + 26;
          ctx.fillStyle = lerpHexColor(stageBg, theme.accentPrimary, 0.32);
          ctx.beginPath();
          ctx.arc(midX - 11.5, midY + 10.5, 2.5, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = lerpHexColor(stageBg, theme.accentSecondary, 0.56);
          ctx.beginPath();
          ctx.arc(midX - 6, midY + 6, 3, 0, Math.PI * 2);
          ctx.fill();

          if (cstyle === 'arrow') {
            drawCursorAlphaSprite(ctx, 'arrow', midX - 8, midY - 11, 1, theme, anim.curStylePhase);
          } else if (cstyle === 'circle') {
            drawCursorAlphaSprite(ctx, 'circle', midX + 1, midY, 1, theme, anim.curStylePhase);
          } else {
            drawCursorAlphaSprite(ctx, 'pointer', midX - 4, midY - 12, 1, theme, anim.curStylePhase);
          }

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          const tw = ctx.measureText(cardTitles[c]).width;
          ctx.fillStyle = isFocused ? theme.textPrimary : theme.textSecondary;
          ctx.fillText(cardTitles[c], Math.round(cx + (cardW - tw) / 2), cardY + 84);

          ctx.font = 'bold 6.8px "JetBrains Mono", monospace';
          const sw = ctx.measureText(cardSubs[c]).width;
          ctx.fillStyle = isFocused ? theme.accentSecondary : theme.textSecondary;
          ctx.fillText(cardSubs[c], Math.round(cx + (cardW - sw) / 2), cardY + 99);
        }

        /* Bottom Live Preview & Status Card (y=172..214) */
        traceRoundedRect(ctx, 14, 172, 292, 42, 6);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();

        const prevBg = theme.isLight ? '#e2e8f0' : '#0c111c';
        traceRoundedRect(ctx, 20, 177, 34, 32, 5);
        ctx.fillStyle = theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 21, 178, 32, 30, 4);
        ctx.fillStyle = prevBg;
        ctx.fill();

        const savedStyle = clampCursorStyleToCard(s.settings.touchpadCursorStyle);
        const phx = savedStyle === 'circle' ? 37 : savedStyle === 'pointer' ? 34 : 30;
        const phy = savedStyle === 'circle' ? 193 : 181;
        drawCursorAlphaSprite(ctx, savedStyle, phx, phy, 1, theme, anim.curStylePhase);

        const isM = s.settings.touchpadMode === 'mouse';
        ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
        ctx.fillStyle = isM ? '#4ade80' : theme.accentSecondary;
        ctx.fillText(
          `Mode : ${isM ? 'Souris' : 'Pave tactile'}  |  Style : ${CURSOR_STYLE_LABELS[savedStyle]}`,
          62,
          186
        );
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('EXE : Appliquer  |  TAB : Retour options', 62, 200);
      }
      break;
    }

    case 'SETTINGS_SHORTCUTS': {
      if (s.games.length === 0) {
        traceRoundedRect(ctx, 40, 88, 240, 44, 7);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('Aucun jeu detecte', 96, 110);
      } else {
        const vis = 6;
        const rowY0 = 32;
        const rowH = 28;
        const rowGap = 2;
        const stride = rowH + rowGap;
        const scrollPx = isActiveScreen
          ? Math.round(anim.listScrollPx)
          : sui.scGameScroll * stride;
        const selY = isActiveScreen
          ? Math.round(anim.selY - anim.listScrollPx)
          : rowY0 + (sui.scGameCursor - sui.scGameScroll) * stride;

        ctx.save();
        ctx.beginPath();
        ctx.rect(0, rowY0 - 2, NL_SCREEN_W, vis * stride - rowGap + 4);
        ctx.clip();

        traceRoundedRect(ctx, 11, selY - 1, 294, rowH + 2, 7);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();

        for (let idx = 0; idx < s.games.length; idx++) {
          const ry = rowY0 + idx * stride - scrollPx;
          if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
          const sel = idx === sui.scGameCursor;
          let rowT = sel ? selGlow : 0;
          if (!sel && isActiveScreen && idx === anim.prevCursor && selGlow < 1) {
            rowT = (1 - selGlow) * 0.5;
          }
          const g = s.games[idx];

          traceRoundedRect(ctx, 12, ry, 292, rowH, 6);
          ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, rowT);
          ctx.fill();

          drawSettingsIconBadge(ctx, 17, ry + 5, 18, 'LAUNCH_KEY', rowT, false, theme);

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          drawEllipsisText(ctx, 41, ry + 14.5, g.title, 214, theme.textPrimary);

          if (g.shortcutKey) {
            traceRoundedRect(ctx, 266, ry + 6, 28, 16, 4);
            ctx.fillStyle = theme.accentPrimary;
            ctx.fill();
            ctx.fillStyle = theme.textOnAccent;
            ctx.fillText(`[${g.shortcutKey}]`, 271, ry + 14.5);
          } else {
            traceRoundedRect(ctx, 266, ry + 6, 28, 16, 4);
            ctx.fillStyle = theme.pillBg;
            ctx.fill();
            ctx.fillStyle = theme.textSecondary;
            ctx.fillText('---', 271, ry + 14.5);
          }
        }
        ctx.restore();

        drawScrollbarV(
          ctx,
          rowY0,
          vis * stride - rowGap,
          scrollPx,
          vis * stride,
          s.games.length * stride,
          theme
        );
      }

      if (sui.scPickingKey && s.games.length > 0) {
        const mEase = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.modalProgress)), 3);
        const dy = Math.round((1 - mEase) * 6);
        ctx.save();
        ctx.globalAlpha = Math.max(0.25, mEase);
        traceRoundedRect(ctx, 22, 42 + dy, 276, 164, 8);
        ctx.fillStyle = theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 24, 44 + dy, 272, 160, 7);
        ctx.fillStyle = theme.bgTopBar;
        ctx.fill();

        const sg = s.games[sui.scGameCursor];
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('Touche pour :', 34, 56 + dy);
        drawEllipsisText(ctx, 114, 56 + dy, sg.title, 172, theme.accentSecondary);

        const cols = 8;
        const total = ASSIGNABLE_SHORTCUT_KEYS.length + 1;
        for (let k = 0; k < total; k++) {
          const r = Math.floor(k / cols);
          const c = k % cols;
          const kx = 34 + c * 31;
          const ky = 72 + dy + r * 26;
          const kw = k === 0 ? 28 : 26;
          const ch = k === 0 ? '-' : ASSIGNABLE_SHORTCUT_KEYS[k - 1];
          const owner = k === 0 ? -1 : findShortcutOwner(s, ch, sui.scGameCursor);

          traceRoundedRect(ctx, kx, ky, kw, 20, 4);
          ctx.fillStyle = owner >= 0 ? theme.borderSubtle : theme.cardBg;
          ctx.fill();
        }

        /* Smooth gliding key-picker highlight pill */
        const hkX = Math.round(anim.scKeyX);
        const hkY = Math.round(anim.scKeyY) + dy;
        const hkW = Math.round(anim.scKeyW);
        traceRoundedRect(ctx, hkX - 1, hkY - 1, hkW + 2, 22, 5);
        ctx.fillStyle = theme.accentSecondary;
        ctx.fill();
        traceRoundedRect(ctx, hkX, hkY, hkW, 20, 4);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();

        for (let k = 0; k < total; k++) {
          const r = Math.floor(k / cols);
          const c = k % cols;
          const kx = 34 + c * 31;
          const ky = 72 + dy + r * 26;
          const ksel = k === sui.scKeyCursor;
          const ch = k === 0 ? '-' : ASSIGNABLE_SHORTCUT_KEYS[k - 1];
          const owner = k === 0 ? -1 : findShortcutOwner(s, ch, sui.scGameCursor);

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          if (k === 0) {
            ctx.fillStyle = ksel ? theme.textOnAccent : theme.textSecondary;
            ctx.fillText('OFF', kx + 4, ky + 10.5);
          } else {
            ctx.fillStyle = ksel
              ? theme.textOnAccent
              : owner >= 0
              ? theme.accentSecondary
              : theme.textPrimary;
            ctx.fillText(ch, kx + 10, ky + 10.5);
          }
        }
        ctx.font = '7.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('Clavier direct ou FLECHES + EXE', 34, 189 + dy);
        ctx.restore();
      }

      if (sui.scConflictPrompt) {
        const mEase = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.modalProgress)), 3);
        const dy = Math.round((1 - mEase) * 6);
        const btnT = anim.modalBtnProgress;
        ctx.save();
        ctx.globalAlpha = Math.max(0.25, mEase);
        traceRoundedRect(ctx, 32, 62 + dy, 256, 120, 8);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();
        traceRoundedRect(ctx, 34, 64 + dy, 252, 116, 7);
        ctx.fillStyle = theme.bgTopBar;
        ctx.fill();

        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText(`Touche [${sui.scConflictKey}] deja assignee a :`, 46, 80 + dy);
        if (sui.scConflictOwnerIdx >= 0 && sui.scConflictOwnerIdx < s.games.length) {
          drawEllipsisText(
            ctx,
            46,
            96 + dy,
            s.games[sui.scConflictOwnerIdx].title,
            228,
            theme.accentSecondary
          );
        }
        ctx.font = '7.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('Remplacer le raccourci ?', 46, 116 + dy);

        traceRoundedRect(ctx, 54, 138 + dy, 92, 24, 5);
        ctx.fillStyle = lerpHexColor(theme.accentPrimary, theme.pillBg, btnT);
        ctx.fill();
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = lerpHexColor(theme.textOnAccent, theme.textPrimary, btnT);
        ctx.fillText('Annuler', 76, 150.5 + dy);

        traceRoundedRect(ctx, 174, 138 + dy, 92, 24, 5);
        ctx.fillStyle = lerpHexColor(theme.pillBg, theme.accentPrimary, btnT);
        ctx.fill();
        ctx.fillStyle = lerpHexColor(theme.textPrimary, theme.textOnAccent, btnT);
        ctx.fillText('Remplacer', 192, 150.5 + dy);
        ctx.restore();
      }
      break;
    }

    case 'SETTINGS_KEY_HELP': {
      const defs = getShortcutDefs(s);
      const vis = 7;
      const rowY0 = 33;
      const rowH = 24;
      const rowGap = 2;
      const stride = rowH + rowGap;
      const scrollPx = isActiveScreen
        ? Math.round(anim.listScrollPx)
        : sui.subScroll * stride;

      ctx.save();
      ctx.beginPath();
      ctx.rect(0, rowY0 - 1, NL_SCREEN_W, vis * stride - rowGap + 2);
      ctx.clip();

      for (let idx = 0; idx < defs.length; idx++) {
        const ry = rowY0 + idx * stride - scrollPx;
        if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
        traceRoundedRect(ctx, 12, ry, 292, rowH, 5);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();

        traceRoundedRect(ctx, 18, ry + 4, 72, 16, 4);
        ctx.fillStyle = theme.pillBg;
        ctx.fill();

        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        drawEllipsisText(ctx, 22, ry + 12.5, defs[idx].keyLabel, 64, theme.accentSecondary);
        drawEllipsisText(ctx, 98, ry + 12.5, defs[idx].actionLabel, 196, theme.textPrimary);
      }
      ctx.restore();

      drawScrollbarV(
        ctx,
        rowY0,
        vis * stride - rowGap,
        scrollPx,
        vis * stride,
        defs.length * stride,
        theme
      );
      break;
    }

    case 'SETTINGS_SORT': {
      const vis = 6;
      const rowY0 = 32;
      const rowH = 28;
      const rowGap = 2;
      const stride = rowH + rowGap;
      const scrollPx = isActiveScreen
        ? Math.round(anim.listScrollPx)
        : sui.subScroll * stride;
      const selY = isActiveScreen
        ? Math.round(anim.selY - anim.listScrollPx)
        : rowY0 + (sui.subCursor - sui.subScroll) * stride;

      ctx.save();
      ctx.beginPath();
      ctx.rect(0, rowY0 - 2, NL_SCREEN_W, vis * stride - rowGap + 4);
      ctx.clip();

      traceRoundedRect(ctx, 11, selY - 1, 294, rowH + 2, 7);
      ctx.fillStyle = theme.accentPrimary;
      ctx.fill();

      for (let idx = 0; idx < SORT_ORDER_LIST.length; idx++) {
        const sortId = SORT_ORDER_LIST[idx];
        const ry = rowY0 + idx * stride - scrollPx;
        if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
        const sel = idx === sui.subCursor;
        let rowT = sel ? selGlow : 0;
        if (!sel && isActiveScreen && idx === anim.prevCursor && selGlow < 1) {
          rowT = (1 - selGlow) * 0.5;
        }
        const cur = sortId === s.settings.sortOrder;

        traceRoundedRect(ctx, 12, ry, 292, rowH, 6);
        ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, rowT);
        ctx.fill();

        ctx.beginPath();
        ctx.arc(25, ry + 14, 5, 0, Math.PI * 2);
        ctx.fillStyle = cur
          ? theme.accentPrimary
          : lerpHexColor(theme.dotInactive, theme.accentSecondary, rowT * 0.45);
        ctx.fill();
        if (cur) {
          ctx.beginPath();
          ctx.arc(25, ry + 14, 2, 0, Math.PI * 2);
          ctx.fillStyle = theme.textOnAccent;
          ctx.fill();
        }

        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText(SORT_ORDER_LABELS[sortId], 36, ry + 14.5);

        if (cur) {
          traceRoundedRect(ctx, 254, ry + 7, 38, 14, 4);
          ctx.fillStyle = theme.pillBg;
          ctx.fill();
          ctx.fillStyle = theme.accentSecondary;
          ctx.fillText('ACTIF', 258, ry + 14.5);
        }
      }
      ctx.restore();

      drawScrollbarV(
        ctx,
        rowY0,
        vis * stride - rowGap,
        scrollPx,
        vis * stride,
        SORT_ORDER_LIST.length * stride,
        theme
      );
      break;
    }

    case 'SETTINGS_STATS': {
      if (s.games.length === 0) {
        traceRoundedRect(ctx, 40, 88, 240, 44, 7);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('Aucune statistique', 96, 110);
      } else {
        const vis = 5;
        const rowY0 = 33;
        const rowH = 33;
        const rowGap = 3;
        const stride = rowH + rowGap;
        const scrollPx = isActiveScreen
          ? Math.round(anim.listScrollPx)
          : sui.subScroll * stride;
        const selY = isActiveScreen
          ? Math.round(anim.selY - anim.listScrollPx)
          : rowY0 + (sui.subCursor - sui.subScroll) * stride;

        ctx.save();
        ctx.beginPath();
        ctx.rect(0, rowY0 - 2, NL_SCREEN_W, vis * stride - rowGap + 4);
        ctx.clip();

        traceRoundedRect(ctx, 11, selY - 1, 294, rowH + 2, 7);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();

        for (let idx = 0; idx < s.games.length; idx++) {
          const ry = rowY0 + idx * stride - scrollPx;
          if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
          const sel = idx === sui.subCursor;
          let rowT = sel ? selGlow : 0;
          if (!sel && isActiveScreen && idx === anim.prevCursor && selGlow < 1) {
            rowT = (1 - selGlow) * 0.5;
          }
          const g = s.games[idx];

          traceRoundedRect(ctx, 12, ry, 292, rowH, 6);
          ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, rowT);
          ctx.fill();

          drawSettingsIconBadge(ctx, 17, ry + 7, 18, 'STATS', rowT, false, theme);

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          drawEllipsisText(ctx, 41, ry + 10, g.title, 174, theme.textPrimary);

          const lStr = `${g.launchCount} lanc.`;
          const lw = ctx.measureText(lStr).width;
          ctx.fillStyle = theme.accentSecondary;
          ctx.fillText(lStr, 294 - lw, ry + 10);

          const line2 = `Temps: ${formatDuration(g.playTimeSec)} · Dernier: ${formatTimestamp(
            g.lastPlayedTs
          )}`;
          ctx.font = '7.5px "JetBrains Mono", monospace';
          drawEllipsisText(ctx, 41, ry + 23.5, line2, 252, theme.textSecondary);
        }
        ctx.restore();

        drawScrollbarV(
          ctx,
          rowY0,
          vis * stride - rowGap,
          scrollPx,
          vis * stride,
          s.games.length * stride,
          theme
        );
      }
      break;
    }

    case 'SETTINGS_CAPTURE_BATCH': {
      traceRoundedRect(ctx, 14, 34, 292, 174, 8);
      ctx.fillStyle = theme.borderSubtle;
      ctx.fill();
      traceRoundedRect(ctx, 16, 36, 288, 170, 7);
      ctx.fillStyle = theme.cardBg;
      ctx.fill();

      drawSettingsIconBadge(ctx, 24, 44, 20, 'CAPTURE', 0.85, false, theme);

      ctx.font = 'bold 8.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textPrimary;
      ctx.fillText(
        sui.capActive
          ? sui.capSemiAuto
            ? 'Mode semi-automatique'
            : 'Generation automatique...'
          : sui.capCancelled
          ? 'Generation interrompue'
          : 'Captures terminees',
        50,
        54
      );

      const rem = sui.capTotal > sui.capPos ? sui.capTotal - sui.capPos : 0;
      const curGameIdx =
        sui.capActive && sui.capPos < sui.capTotal ? sui.capQueue[sui.capPos] : -1;
      const curTitle =
        curGameIdx >= 0 && curGameIdx < s.games.length
          ? s.games[curGameIdx].title
          : sui.capTotal === 0
          ? 'Toutes les captures existent deja'
          : 'Traitement termine';

      ctx.font = '7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Jeu en cours :', 24, 78);
      ctx.font = 'bold 8px "JetBrains Mono", monospace';
      drawEllipsisText(ctx, 108, 78, curTitle, 184, theme.accentSecondary);

      /* Progress bar */
      const barX = 24;
      const barY = 94;
      const barW = 272;
      const barH = 12;
      traceRoundedRect(ctx, barX, barY, barW, barH, 6);
      ctx.fillStyle = theme.pillBg;
      ctx.fill();

      const progRatio =
        sui.capTotal > 0 ? Math.max(0, Math.min(1, sui.capPos / sui.capTotal)) : 1;
      const fillW = Math.round(barW * progRatio);
      if (fillW > 0) {
        traceRoundedRect(ctx, barX, barY, Math.max(8, fillW), barH, 6);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();
      }

      ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textPrimary;
      ctx.fillText(
        `Captures : ${sui.capDoneCount}   |   Restants : ${rem}   |   Passes : ${sui.capSkipCount}`,
        24,
        122
      );

      ctx.font = '7.2px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      if (sui.capSemiAuto) {
        ctx.fillStyle = theme.accentSecondary;
        ctx.fillText('Ce jeu necessite une fermeture manuelle [ESC]', 24, 142);
        ctx.fillText('ou appuyez sur [DROITE] pour le passer.', 24, 154);
      } else if (sui.capActive) {
        ctx.fillText('Chaque jeu sans capture (.scr.tns) est ouvert,', 24, 142);
        ctx.fillText('capture automatiquement en VRAM puis referme.', 24, 154);
      } else {
        ctx.fillText('Les captures deja realisees restent conservees.', 24, 142);
        ctx.fillText('Elles apparaissent en fondu au lancement.', 24, 154);
      }

      if (sui.capActive) {
        traceRoundedRect(ctx, 28, 170, 124, 24, 5);
        ctx.fillStyle = theme.pillBg;
        ctx.fill();
        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.accentSecondary;
        ctx.fillText('DROITE : Passer', 44, 182.5);

        traceRoundedRect(ctx, 168, 170, 124, 24, 5);
        ctx.fillStyle = '#dc2626';
        ctx.fill();
        ctx.fillStyle = '#ffffff';
        ctx.fillText('ESC : Annuler', 190, 182.5);
      } else {
        traceRoundedRect(ctx, 76, 170, 168, 24, 5);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();
        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textOnAccent;
        ctx.fillText('EXE / ESC : Retour', 112, 182.5);
      }
      break;
    }

    case 'SETTINGS_ICONS': {
      if (s.games.length === 0) {
        traceRoundedRect(ctx, 40, 88, 240, 44, 7);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText('Aucun jeu detecte', 96, 110);
      } else {
        const cols = 3;
        const visRows = 2;
        const totalRows = Math.ceil(s.games.length / cols);
        const cardW = 68;
        const cardH = 68;
        const gapX = 22;
        const startX = 32;
        const startY = 33;
        const rowStride = 90;
        const scrollPx = isActiveScreen
          ? Math.round(anim.listScrollPx)
          : sui.iconScrollRow * rowStride;

        ctx.save();
        ctx.beginPath();
        ctx.rect(0, 28, NL_SCREEN_W, 188);
        ctx.clip();

        for (let idx = 0; idx < s.games.length; idx++) {
          const g = s.games[idx];
          const c = idx % cols;
          const r = Math.floor(idx / cols);
          const cx = startX + c * (cardW + gapX);
          const cy = startY + r * rowStride - scrollPx;
          if (cy + cardH + 18 < 28 || cy > 216) continue;

          const sel = idx === sui.iconCursor;
          if (sel) {
            traceRoundedRect(ctx, cx - 2, cy - 2, cardW + 4, cardH + 4, 11);
            ctx.fillStyle = theme.accentPrimary;
            ctx.fill();
          }

          drawGameCoverCard(
            ctx,
            cx,
            cy,
            cardW,
            cardH,
            9,
            g,
            sel,
            sel ? 0.85 : 0,
            theme,
            imgCacheRef_global
          );

          /* Custom icon badge in top-right corner of the card */
          const hasCustom = g.iconSource !== 'NONE' && Boolean(g.capturedIconDataUrl);
          const tagStr = g.iconSource === 'BMP' ? 'BMP' : hasCustom ? 'ICO' : 'DEF';
          traceRoundedRect(ctx, cx + cardW - 24, cy + 3, 21, 11, 3);
          ctx.fillStyle = hasCustom ? theme.accentPrimary : theme.pillBg;
          ctx.fill();
          ctx.font = 'bold 6.5px "JetBrains Mono", monospace';
          ctx.fillStyle = hasCustom ? theme.textOnAccent : theme.textSecondary;
          ctx.fillText(tagStr, cx + cardW - 21, cy + 9);

          ctx.font = 'bold 7px "JetBrains Mono", monospace';
          const labelMaxW = cardW + 14;
          let dispTitle = g.title;
          if (ctx.measureText(dispTitle).width > labelMaxW) {
            while (dispTitle.length > 1 && ctx.measureText(dispTitle + '..').width > labelMaxW) {
              dispTitle = dispTitle.slice(0, -1);
            }
            dispTitle += '..';
          }
          const titleW = ctx.measureText(dispTitle).width;
          ctx.fillStyle = sel ? theme.textPrimary : theme.textSecondary;
          ctx.fillText(dispTitle, Math.round(cx + (cardW - titleW) / 2), cy + cardH + 9);
        }
        ctx.restore();

        drawScrollbarV(
          ctx,
          startY,
          visRows * rowStride - 10,
          scrollPx,
          visRows * rowStride,
          totalRows * rowStride,
          theme
        );
      }

      if (sui.iconConfirmModal && sui.iconCursor >= 0 && sui.iconCursor < s.games.length) {
        const mEase = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.modalProgress)), 3);
        const dy = Math.round((1 - mEase) * 6);
        const btnT = anim.modalBtnProgress;
        const targetGame = s.games[sui.iconCursor];

        ctx.save();
        ctx.globalAlpha = Math.max(0.25, mEase);
        traceRoundedRect(ctx, 28, 54 + dy, 264, 126, 8);
        ctx.fillStyle = '#dc2626';
        ctx.fill();
        traceRoundedRect(ctx, 30, 56 + dy, 260, 122, 7);
        ctx.fillStyle = theme.bgTopBar;
        ctx.fill();

        ctx.font = 'bold 8.5px "JetBrains Mono", monospace';
        ctx.fillStyle = '#f87171';
        ctx.fillText("Supprimer l'icone personnalisee ?", 44, 73 + dy);

        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        drawEllipsisText(ctx, 44, 91 + dy, targetGame.title, 232, theme.textPrimary);

        ctx.font = '7.2px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText("Le jeu (.tns) et sa capture d'ecran (.scr.tns)", 44, 109 + dy);
        ctx.fillText("seront integralement conserves.", 44, 121 + dy);

        traceRoundedRect(ctx, 48, 138 + dy, 98, 24, 5);
        ctx.fillStyle = lerpHexColor(theme.accentPrimary, theme.pillBg, btnT);
        ctx.fill();
        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = lerpHexColor(theme.textOnAccent, theme.textPrimary, btnT);
        ctx.fillText('Annuler', 74, 150.5 + dy);

        traceRoundedRect(ctx, 174, 138 + dy, 98, 24, 5);
        ctx.fillStyle = lerpHexColor(theme.pillBg, '#dc2626', btnT);
        ctx.fill();
        ctx.fillStyle = lerpHexColor(theme.textPrimary, '#ffffff', btnT);
        ctx.fillText('Supprimer', 194, 150.5 + dy);
        ctx.restore();
      }
      break;
    }

    case 'SETTINGS_RESET': {
      const mEase = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.modalProgress)), 3);
      const dy = Math.round((1 - mEase) * 6);
      const btnT = anim.modalBtnProgress;

      ctx.save();
      ctx.globalAlpha = Math.max(0.25, mEase);
      traceRoundedRect(ctx, 16, 36 + dy, 288, 172, 8);
      ctx.fillStyle = theme.borderSubtle;
      ctx.fill();
      traceRoundedRect(ctx, 18, 38 + dy, 284, 168, 7);
      ctx.fillStyle = theme.cardBg;
      ctx.fill();

      drawSettingsIconBadge(ctx, 28, 43 + dy, 18, 'RESET', 0.85, true, theme);

      ctx.font = 'bold 8.5px "JetBrains Mono", monospace';
      ctx.fillStyle = '#f87171';
      ctx.fillText('Reinitialiser les reglages ?', 52, 52 + dy);

      ctx.font = '7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textPrimary;
      ctx.fillText('Cette operation reinitialisera uniquement', 30, 72 + dy);
      ctx.fillText('les parametres de N Launcher.', 30, 84 + dy);

      ctx.fillStyle = theme.accentSecondary;
      ctx.fillText('Aucun jeu, fichier, sauvegarde ou donnee', 30, 102 + dy);
      ctx.fillText('personnelle ne sera supprime.', 30, 114 + dy);

      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Seules les valeurs de configuration', 30, 132 + dy);
      ctx.fillText('seront restaurees par defaut.', 30, 144 + dy);

      traceRoundedRect(ctx, 42, 166 + dy, 108, 26, 6);
      ctx.fillStyle = lerpHexColor(theme.accentPrimary, theme.pillBg, btnT);
      ctx.fill();
      ctx.font = 'bold 8px "JetBrains Mono", monospace';
      ctx.fillStyle = lerpHexColor(theme.textOnAccent, theme.textPrimary, btnT);
      ctx.fillText('Annuler', 74, 179.5 + dy);

      traceRoundedRect(ctx, 170, 166 + dy, 108, 26, 6);
      ctx.fillStyle = lerpHexColor(theme.pillBg, '#dc2626', btnT);
      ctx.fill();
      ctx.fillStyle = lerpHexColor(theme.textPrimary, '#ffffff', btnT);
      ctx.fillText('Confirmer', 196, 179.5 + dy);
      ctx.restore();
      break;
    }

    case 'SETTINGS_INFO': {
      const favCount = s.games.filter((g) => g.isFavorite).length;
      const ndlessStr = s.sys.hasNdless
        ? `r${s.sys.ndlessRev} (${s.sys.ndlessPersistent ? 'Persistant' : 'Temporaire'})`
        : 'Non detecte';
      const rows: [string, string, SettingsIconId][] = [
        ['Logiciel', 'N Launcher', 'CREDITS'],
        ['Version', `${NL_LAUNCHER_VERSION_STR} (${NL_BUILD_STAMP_STR})`, 'INFO'],
        ['Systeme OS', s.sys.osVersion || 'TI-Nspire OS', 'SCREEN'],
        ['Ndless', ndlessStr, 'POWER'],
        [
          'Bibliotheque',
          `${s.games.length} jeux (${s.sys.cacheValid ? 'Cache CRC OK' : 'Scan direct'})`,
          'GRID',
        ],
        ['Favoris', `${favCount} favoris`, 'STATS'],
      ];

      rows.forEach(([k, v, ico], i) => {
        const ry = 34 + i * 29;
        traceRoundedRect(ctx, 14, ry, 292, 25, 6);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();
        drawSettingsIconBadge(ctx, 19, ry + 4, 17, ico, 0.3, false, theme);
        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText(k, 42, ry + 13);
        drawEllipsisText(
          ctx,
          134,
          ry + 13,
          v,
          164,
          i === 0 || i === 1 ? theme.accentSecondary : theme.textPrimary
        );
      });
      break;
    }

    case 'SETTINGS_CREDITS': {
      traceRoundedRect(ctx, 28, 42, 264, 158, 9);
      ctx.fillStyle = theme.borderSubtle;
      ctx.fill();
      traceRoundedRect(ctx, 30, 44, 260, 154, 8);
      ctx.fillStyle = theme.cardBg;
      ctx.fill();

      drawLauncherEmblem(ctx, 143, 56, 34, theme);

      ctx.textAlign = 'center';
      ctx.font = 'bold 10px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textPrimary;
      ctx.fillText('N Launcher', 160, 104);

      ctx.font = '7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Cree et developpe par', 160, 122);

      ctx.font = 'bold 9.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.accentPrimary;
      ctx.fillText('daniel.b', 160, 138);

      ctx.font = '7.5px "JetBrains Mono", monospace';
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText(`(c) 2026  -  v${NL_LAUNCHER_VERSION_STR} (${NL_BUILD_STAMP_STR})`, 160, 160);

      ctx.fillStyle = theme.accentSecondary;
      ctx.fillText('TI-Nspire CX II-T  -  Ndless', 160, 178);
      ctx.textAlign = 'left';
      break;
    }

    case 'SETTINGS_PROFILES': {
      const profiles = s.profiles.items;
      const pcount = profiles.length;
      const mode = sui.profMode;

      if (mode === 'LIST') {
        const totalRows = pcount + 1;
        const vis = 6;
        const rowY0 = 32;
        const rowH = 28;
        const rowGap = 2;
        const stride = rowH + rowGap;
        const scrollPx = isActiveScreen ? Math.round(anim.listScrollPx) : sui.profScroll * stride;
        const selY = isActiveScreen
          ? Math.round(anim.selY - anim.listScrollPx)
          : rowY0 + (sui.profCursor - sui.profScroll) * stride;

        ctx.save();
        ctx.beginPath();
        ctx.rect(0, rowY0 - 2, NL_SCREEN_W, vis * stride - rowGap + 4);
        ctx.clip();

        traceRoundedRect(ctx, 11, selY - 1, 294, rowH + 2, 7);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();

        for (let row = 0; row < totalRows; row++) {
          const ry = rowY0 + row * stride - scrollPx;
          if (ry + rowH < rowY0 - 2 || ry > rowY0 + vis * stride + 2) continue;
          const sel = row === sui.profCursor;
          let rowT = sel ? selGlow : 0;
          if (!sel && isActiveScreen && row === anim.prevCursor && selGlow < 1) {
            rowT = (1 - selGlow) * 0.5;
          }

          traceRoundedRect(ctx, 12, ry, 292, rowH, 6);
          ctx.fillStyle = lerpHexColor(theme.cardBg, theme.cardBgSel, rowT);
          ctx.fill();

          if (row === 0) {
            ctx.beginPath();
            ctx.arc(28, ry + 14, 10, 0, Math.PI * 2);
            ctx.fillStyle = lerpHexColor(theme.pillBg, theme.accentPrimary, rowT);
            ctx.fill();

            const plusCol = sel ? theme.textOnAccent : theme.accentSecondary;
            ctx.fillStyle = plusCol;
            ctx.fillRect(27, ry + 9, 2, 10);
            ctx.fillRect(23, ry + 13, 10, 2);

            ctx.font = 'bold 8px "JetBrains Mono", monospace';
            drawEllipsisText(ctx, 44, ry + 9.5, 'Creer un profil', 180, theme.textPrimary);
            ctx.font = '6.8px "JetBrains Mono", monospace';
            drawEllipsisText(ctx, 44, ry + 20, 'Choisir un nom et une icone', 190, theme.textSecondary);

            const cntStr = `${pcount}/${NL_MAX_PROFILES}`;
            ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
            const cw = Math.ceil(ctx.measureText(cntStr).width) + 8;
            traceRoundedRect(ctx, 286 - cw, ry + 7, cw, 14, 4);
            ctx.fillStyle = theme.pillBg;
            ctx.fill();
            ctx.fillStyle = theme.accentSecondary;
            ctx.fillText(cntStr, 290 - cw, ry + 14.5);
          } else {
            const pi = row - 1;
            const p = profiles[pi];
            const isAct = pi === s.profiles.activeIndex;
            if (isAct) {
              ctx.beginPath();
              ctx.arc(28, ry + 14, 11, 0, Math.PI * 2);
              ctx.fillStyle = theme.accentPrimary;
              ctx.fill();
            }
            drawCircularAvatar(ctx, 18, ry + 4, 20, p.avatarId, false, theme, imgCacheRef_global);

            ctx.font = 'bold 8px "JetBrains Mono", monospace';
            drawEllipsisText(ctx, 44, ry + 9.5, p.name, 174, theme.textPrimary);
            const sub = p.isPrimary
              ? 'Profil principal'
              : getAvatarCategoryLabel(getAvatarCategoryOf(p.avatarId));
            ctx.font = '6.8px "JetBrains Mono", monospace';
            drawEllipsisText(ctx, 44, ry + 20, sub, 174, theme.textSecondary);

            if (isAct) {
              traceRoundedRect(ctx, 246, ry + 7, 38, 14, 4);
              ctx.fillStyle = theme.accentPrimary;
              ctx.fill();
              ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
              ctx.fillStyle = theme.textOnAccent;
              ctx.fillText('ACTIF', 250, ry + 14.5);
            }
            ctx.font = 'bold 8px "JetBrains Mono", monospace';
            ctx.fillStyle = lerpHexColor(theme.textSecondary, theme.accentPrimary, rowT);
            ctx.fillText('>', 291, ry + 14.5);
          }
        }
        ctx.restore();

        drawScrollbarV(
          ctx,
          rowY0,
          vis * stride - rowGap,
          scrollPx,
          vis * stride,
          totalRows * stride,
          theme
        );
      } else if (mode === 'DETAIL' || mode === 'CONFIRM_DELETE') {
        const di = Math.min(Math.max(0, sui.profDetailIdx), Math.max(0, pcount - 1));
        const dp = profiles[di] ?? getActiveProfile(s);
        const isAct = di === s.profiles.activeIndex;
        const canDel = !dp.isPrimary && pcount > 1;

        /* Top summary card */
        traceRoundedRect(ctx, 12, 31, 296, 46, 7);
        ctx.fillStyle = theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 13, 32, 294, 44, 6);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();

        if (isAct) {
          ctx.beginPath();
          ctx.arc(37, 54, 18, 0, Math.PI * 2);
          ctx.fillStyle = theme.accentPrimary;
          ctx.fill();
        }
        drawCircularAvatar(ctx, 20, 37, 34, dp.avatarId, false, theme, imgCacheRef_global);

        ctx.font = 'bold 8.5px "JetBrains Mono", monospace';
        drawEllipsisText(ctx, 62, 46, dp.name, 178, theme.textPrimary);
        ctx.font = '7.2px "JetBrains Mono", monospace';
        drawEllipsisText(
          ctx,
          62,
          60,
          `${dp.isPrimary ? 'Principal' : 'Profil libre'}  |  ${getAvatarCategoryLabel(
            getAvatarCategoryOf(dp.avatarId)
          )}`,
          224,
          theme.textSecondary
        );
        if (isAct) {
          traceRoundedRect(ctx, 260, 40, 38, 14, 4);
          ctx.fillStyle = theme.accentPrimary;
          ctx.fill();
          ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
          ctx.fillStyle = theme.textOnAccent;
          ctx.fillText('ACTIF', 264, 47.5);
        }

        const actLabels = [
          'Modifier le nom',
          "Changer l'icone",
          'Activer ce profil',
          'Supprimer le profil',
        ];
        const rowY0 = 83;
        const rowH = 28;
        const stride = 32;
        for (let r = 0; r < 4; r++) {
          const ry = rowY0 + r * stride;
          const sel = r === sui.profDetailCursor;
          const danger = r === 3 && canDel;
          const locked = r === 3 && !canDel;
          const bCol = sel ? (danger ? '#dc2626' : theme.accentPrimary) : theme.borderSubtle;
          traceRoundedRect(ctx, 12, ry - 1, 296, rowH + 2, 7);
          ctx.fillStyle = bCol;
          ctx.fill();
          traceRoundedRect(ctx, 13, ry, 294, rowH, 6);
          ctx.fillStyle = sel ? theme.cardBgSel : theme.cardBg;
          ctx.fill();

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          ctx.fillStyle = danger ? '#f87171' : locked ? theme.textSecondary : theme.textPrimary;
          ctx.fillText(actLabels[r], 24, ry + 14.5);

          let rpill = '';
          if (r === 0) rpill = dp.name;
          else if (r === 1) rpill = `#${dp.avatarId + 1}`;
          else if (r === 2) rpill = isAct ? 'Actif' : 'Basculer';
          else rpill = canDel ? 'Suppr.' : 'Protege';

          ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
          const pw = Math.min(106, Math.ceil(ctx.measureText(rpill).width) + 10);
          traceRoundedRect(ctx, 284 - pw, ry + 7, pw, 14, 4);
          ctx.fillStyle = danger ? '#7f1d1d' : theme.pillBg;
          ctx.fill();
          drawEllipsisText(
            ctx,
            289 - pw,
            ry + 14.5,
            rpill,
            pw - 8,
            danger ? '#fecaca' : theme.accentSecondary
          );

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          ctx.fillStyle = sel ? theme.accentPrimary : theme.textSecondary;
          ctx.fillText('>', 291, ry + 14.5);
        }

        if (mode === 'CONFIRM_DELETE') {
          const mEase = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.modalProgress)), 3);
          const dy = Math.round((1 - mEase) * 6);
          const btnT = anim.modalBtnProgress;

          ctx.save();
          ctx.fillStyle = theme.isLight
            ? `rgba(180, 190, 205, ${0.44 * mEase})`
            : `rgba(6, 8, 14, ${0.48 * mEase})`;
          ctx.fillRect(0, 26, NL_SCREEN_W, 194);

          ctx.globalAlpha = Math.max(0.25, mEase);
          traceRoundedRect(ctx, 20, 44 + dy, 280, 146, 8);
          ctx.fillStyle = '#dc2626';
          ctx.fill();
          traceRoundedRect(ctx, 22, 46 + dy, 276, 142, 7);
          ctx.fillStyle = theme.bgTopBar;
          ctx.fill();

          ctx.font = 'bold 8.5px "JetBrains Mono", monospace';
          ctx.fillStyle = '#f87171';
          ctx.fillText('Supprimer ce profil ?', 34, 60 + dy);

          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          drawEllipsisText(ctx, 34, 76 + dy, dp.name, 250, theme.textPrimary);

          ctx.font = '7.2px "JetBrains Mono", monospace';
          ctx.fillStyle = theme.textSecondary;
          ctx.fillText('Favoris, stats et raccourcis du profil', 34, 96 + dy);
          ctx.fillText('seront effaces et son icone liberee.', 34, 108 + dy);
          ctx.fillStyle = theme.accentSecondary;
          ctx.fillText('Les jeux (.tns) et sauvegardes restent.', 34, 124 + dy);

          traceRoundedRect(ctx, 42, 148 + dy, 104, 24, 5);
          ctx.fillStyle = lerpHexColor(theme.accentPrimary, theme.pillBg, btnT);
          ctx.fill();
          ctx.font = 'bold 8px "JetBrains Mono", monospace';
          ctx.fillStyle = lerpHexColor(theme.textOnAccent, theme.textPrimary, btnT);
          ctx.fillText('Annuler', 70, 160.5 + dy);

          traceRoundedRect(ctx, 174, 148 + dy, 104, 24, 5);
          ctx.fillStyle = lerpHexColor(theme.pillBg, '#dc2626', btnT);
          ctx.fill();
          ctx.fillStyle = lerpHexColor(theme.textPrimary, '#ffffff', btnT);
          ctx.fillText('Supprimer', 196, 160.5 + dy);
          ctx.restore();
        }
      } else if (mode === 'EDIT_NAME' || mode === 'CREATE_NAME') {
        const isCreate = mode === 'CREATE_NAME';
        traceRoundedRect(ctx, 18, 42, 284, 148, 8);
        ctx.fillStyle = theme.borderSubtle;
        ctx.fill();
        traceRoundedRect(ctx, 20, 44, 280, 144, 7);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();

        ctx.font = 'bold 8px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textPrimary;
        ctx.fillText(
          isCreate ? 'Nouveau profil (1/2) — Nom :' : 'Nom du profil (max 15 car.) :',
          34,
          62
        );

        traceRoundedRect(ctx, 34, 80, 252, 32, 6);
        ctx.fillStyle = theme.accentPrimary;
        ctx.fill();
        traceRoundedRect(ctx, 36, 82, 248, 28, 5);
        ctx.fillStyle = theme.bgMain;
        ctx.fill();

        ctx.font = 'bold 9px "JetBrains Mono", monospace';
        drawEllipsisText(ctx, 46, 96, `${sui.profEditName}_`, 230, theme.textPrimary);

        ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText(`${sui.profEditName.length}/${NL_PROFILE_NAME_MAX}`, 254, 122);

        ctx.fillText('BACKSPACE Effacer  |  ESPACE Espace', 34, 142);
        ctx.fillStyle = theme.accentSecondary;
        ctx.fillText(
          isCreate ? 'Etape suivante : choisir une icone' : 'Appuyez sur EXE pour enregistrer',
          34,
          158
        );
      } else {
        const isCreate = mode === 'CREATE_AVATAR';
        const cat = Math.min(NL_AVATAR_CAT_COUNT - 1, Math.max(0, sui.profAvatarCat));
        const tabW = 72;

        ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
        for (let c = 0; c < NL_AVATAR_CAT_COUNT; c++) {
          const tx = 11 + c * (tabW + 3);
          const csel = c === cat;
          traceRoundedRect(ctx, tx, 30, tabW, 16, 4);
          ctx.fillStyle = csel ? theme.accentPrimary : theme.pillBg;
          ctx.fill();
          const lbl = getAvatarCategoryLabel(c);
          const lw = ctx.measureText(lbl).width;
          ctx.fillStyle = csel ? theme.textOnAccent : theme.textSecondary;
          ctx.fillText(lbl, Math.round(tx + (tabW - lw) / 2), 38.5);
        }

        const { start: cstart, count: ccnt } = getAvatarCategoryRange(cat);
        const cols = cat === 0 ? 8 : 5;
        const diam = cat === 0 ? 31 : 36;
        const dxStep = cat === 0 ? 37 : 52;
        const dyStep = cat === 0 ? 54 : 58;
        const gridW = (cols - 1) * dxStep + diam;
        const startX = Math.floor((NL_SCREEN_W - gridW) / 2);
        const startY = 58;
        const excl = isCreate ? -1 : sui.profDetailIdx;

        for (let i = 0; i < ccnt; i++) {
          const avId = cstart + i;
          const r = Math.floor(i / cols);
          const c = i % cols;
          const ax = startX + c * dxStep;
          const ay = startY + r * dyStep;
          const acx = ax + diam / 2;
          const acy = ay + diam / 2;
          const owner = findAvatarOwner(s, avId, excl);
          const taken = owner >= 0;
          const cur = i === sui.profAvatarCursor;

          if (cur) {
            drawMulticolorFocusRing(
              ctx,
              acx,
              acy,
              diam + 2,
              2.2,
              anim.qpGlowPhase,
              theme.isLight,
              1
            );
          }
          drawCircularAvatar(ctx, ax, ay, diam, avId, taken, theme, imgCacheRef_global);

          if (taken) {
            traceRoundedRect(ctx, acx - 13, ay + diam - 9, 26, 10, 3);
            ctx.fillStyle = '#b41e1e';
            ctx.fill();
            ctx.font = 'bold 6.8px "JetBrains Mono", monospace';
            ctx.fillStyle = '#ffffff';
            ctx.fillText('PRIS', acx - 9.5, ay + diam - 4);
          }
        }

        const hovId = cstart + sui.profAvatarCursor;
        const hovOwner = findAvatarOwner(s, hovId, excl);
        traceRoundedRect(ctx, 14, 192, 292, 20, 5);
        ctx.fillStyle = theme.cardBg;
        ctx.fill();
        ctx.font = 'bold 7.2px "JetBrains Mono", monospace';
        if (hovOwner >= 0) {
          drawEllipsisText(
            ctx,
            22,
            202.5,
            `Icone deja utilisee par : ${profiles[hovOwner]?.name ?? ''}`,
            276,
            '#f87171'
          );
        } else {
          drawEllipsisText(
            ctx,
            22,
            202.5,
            `Categorie ${getAvatarCategoryLabel(cat)}  -  Icone disponible`,
            276,
            theme.accentSecondary
          );
        }
      }
      break;
    }
  }
}

function drawSettingsView(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette,
  activeScreen: SettingsScreenId
) {
  const openEase =
    anim.setOpenProgress >= 0.995
      ? 1
      : anim.setOpenProgress <= 0
      ? 0
      : (() => {
          const t = anim.setOpenProgress;
          const inv = 1 - t;
          const cubic = 1 - inv * inv * inv;
          return t * 0.3 + cubic * 0.7;
        })();
  const gearRot = openEase * Math.PI * 0.75;
  const meta = getScreenChromeMeta(activeScreen);
  drawSettingsChrome(ctx, theme, meta.title, meta.icon, meta.hints, gearRot);

  if (
    anim.secTransProgress < 0.99 &&
    anim.prevUiScreen !== 'LIBRARY' &&
    anim.prevUiScreen !== activeScreen
  ) {
    const ease = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.secTransProgress)), 3);
    const outShift = -anim.secTransDir * Math.round(ease * 14);
    const inShift = anim.secTransDir * Math.round((1 - ease) * 14);

    ctx.save();
    ctx.beginPath();
    ctx.rect(0, NL_VIEW_TOP, NL_SCREEN_W, NL_BOTTOM_BAR_Y - 1 - NL_VIEW_TOP);
    ctx.clip();

    ctx.save();
    ctx.globalAlpha = 1 - ease;
    ctx.translate(outShift, 0);
    drawSingleSettingsSectionBody(ctx, s, anim.prevUiScreen, anim, theme, false);
    ctx.restore();

    ctx.save();
    ctx.globalAlpha = ease;
    ctx.translate(inShift, 0);
    drawSingleSettingsSectionBody(ctx, s, activeScreen, anim, theme, true);
    ctx.restore();

    ctx.restore();
  } else {
    drawSingleSettingsSectionBody(ctx, s, activeScreen, anim, theme, true);
  }
}

function renderToastBanner(
  ctx: CanvasRenderingContext2D,
  anim: { toastStartMs: number; activeToastText: string | null },
  nowMs: number,
  theme: ConsoleThemePalette
) {
  if (!anim.activeToastText) return;
  const tAge = nowMs - anim.toastStartMs;
  const dur = 2000;
  if (tAge >= dur) {
    anim.activeToastText = null;
    return;
  }

  let ty = 29;
  let alpha = 1;
  if (tAge < 150) {
    const p = Math.max(0, Math.min(1, tAge / 150));
    const ease = 1 - Math.pow(1 - p, 3);
    ty = Math.round(17 + ease * 12);
    alpha = ease;
  } else if (tAge > dur - 180) {
    const p = Math.max(0, Math.min(1, (dur - tAge) / 180));
    const ease = 1 - Math.pow(1 - p, 3);
    ty = Math.round(17 + ease * 12);
    alpha = ease;
  }

  const tw = Math.min(260, anim.activeToastText.length * 5.8 + 20);
  const tx = Math.round((NL_SCREEN_W - tw) / 2);
  ctx.save();
  ctx.globalAlpha = Math.max(0.05, alpha);
  traceRoundedRect(ctx, tx, ty, tw, 17, 5);
  ctx.fillStyle = theme.bgTopBar;
  ctx.fill();
  ctx.strokeStyle = theme.borderSubtle;
  ctx.lineWidth = 1;
  ctx.stroke();
  ctx.fillStyle = theme.accentSecondary;
  ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
  ctx.textBaseline = 'middle';
  ctx.fillText(anim.activeToastText, tx + 10, ty + 8.8);
  ctx.restore();
}

function stepSettingsAnimations(s: LauncherState, anim: SettingsAnimState) {
  const targetHk = s.settings.globalHotkey ? 1 : 0;
  anim.switchHotkeyAnim += (targetHk - anim.switchHotkeyAnim) * 0.28;
  if (Math.abs(targetHk - anim.switchHotkeyAnim) < 0.008) anim.switchHotkeyAnim = targetHk;

  const targetNames = s.settings.showNames ? 1 : 0;
  anim.switchShowNamesAnim += (targetNames - anim.switchShowNamesAnim) * 0.28;
  if (Math.abs(targetNames - anim.switchShowNamesAnim) < 0.008) anim.switchShowNamesAnim = targetNames;

  const targetSc = s.settings.showShortcuts ? 1 : 0;
  anim.switchShortcutsAnim += (targetSc - anim.switchShortcutsAnim) * 0.28;
  if (Math.abs(targetSc - anim.switchShortcutsAnim) < 0.008) anim.switchShortcutsAnim = targetSc;

  const targetTp = s.settings.touchpadNav ? 1 : 0;
  anim.switchTouchpadAnim += (targetTp - anim.switchTouchpadAnim) * 0.28;
  if (Math.abs(targetTp - anim.switchTouchpadAnim) < 0.008) anim.switchTouchpadAnim = targetTp;

  const targetTpMode = s.settings.touchpadMode === 'mouse' ? 1 : 0;
  anim.switchTouchpadModeAnim += (targetTpMode - anim.switchTouchpadModeAnim) * 0.28;
  if (Math.abs(targetTpMode - anim.switchTouchpadModeAnim) < 0.008) {
    anim.switchTouchpadModeAnim = targetTpMode;
  }

  anim.curStylePhase = (anim.curStylePhase + 0.14) % (Math.PI * 2);

  const targetTpTab = s.sui.tpTab === 1 ? 1 : 0;
  anim.tpTabTransAnim += (targetTpTab - anim.tpTabTransAnim) * 0.34;
  if (Math.abs(targetTpTab - anim.tpTabTransAnim) < 0.01) anim.tpTabTransAnim = targetTpTab;

  if (s.sui.screen === 'SETTINGS_TOUCHPAD' && s.sui.tpTab === 1) {
    anim.tpStyleHaloPhase = (anim.tpStyleHaloPhase + 0.14) % (Math.PI * 2);
    if (s.sui.tpStyleCursor !== anim.lastTpStyleCursor) {
      anim.lastTpStyleCursor = s.sui.tpStyleCursor;
      anim.tpStyleGlowAnim = 0.25;
    } else if (anim.tpStyleGlowAnim < 1) {
      anim.tpStyleGlowAnim = Math.min(1, anim.tpStyleGlowAnim + 0.18);
    }
  } else {
    anim.tpStyleGlowAnim = 1;
    anim.lastTpStyleCursor = s.sui.tpStyleCursor;
  }

  /* Mode 2 (Souris) & CTRL Temporary Mouse Cursor Inertia + 5-node luminous trail.
   * Mode 1 (Pave tactile) WITHOUT CTRL strictly disables & clears the cursor & trail! */
  {
    const tgt = computeTouchpadCursorTarget(s);
    const vis = isTouchpadCursorVisible(s);
    if (!vis) {
      anim.curOpacity = 0;
      anim.curIdleTicks = 0;
      for (let i = 0; i < 5; i++) {
        anim.curTrailAlpha[i] = 0;
      }
      if (!anim.curPosInit) {
        anim.curPosInit = true;
        anim.curX = tgt.x;
        anim.curY = tgt.y;
      }
    } else {
      if (!anim.curPosInit) {
        anim.curPosInit = true;
        anim.curX = tgt.x;
        anim.curY = tgt.y;
        anim.curVx = 0;
        anim.curVy = 0;
        for (let i = 0; i < 5; i++) {
          anim.curTrailX[i] = tgt.x;
          anim.curTrailY[i] = tgt.y;
          anim.curTrailAlpha[i] = 0;
        }
      }

      const dx = tgt.x - anim.curX;
      const dy = tgt.y - anim.curY;
      const moving = Math.abs(dx) > 0.4 || Math.abs(dy) > 0.4;

      if (moving) {
        for (let i = 4; i > 0; i--) {
          anim.curTrailX[i] = anim.curTrailX[i - 1];
          anim.curTrailY[i] = anim.curTrailY[i - 1];
          anim.curTrailAlpha[i] = anim.curTrailAlpha[i - 1] * 0.72;
        }
        anim.curTrailX[0] = anim.curX;
        anim.curTrailY[0] = anim.curY;
        const speed = Math.min(1, (Math.abs(dx) + Math.abs(dy)) / 18);
        anim.curTrailAlpha[0] = Math.max(0.32, speed * 0.85);

        anim.curVx = dx * 0.56;
        anim.curVy = dy * 0.56;
        anim.curX += anim.curVx;
        anim.curY += anim.curVy;
        if (Math.abs(tgt.x - anim.curX) < 0.45) anim.curX = tgt.x;
        if (Math.abs(tgt.y - anim.curY) < 0.45) anim.curY = tgt.y;
        anim.curIdleTicks = 0;
      } else {
        anim.curX = tgt.x;
        anim.curY = tgt.y;
        anim.curVx = 0;
        anim.curVy = 0;
        for (let i = 0; i < 5; i++) {
          anim.curTrailAlpha[i] = Math.max(0, anim.curTrailAlpha[i] * 0.7 - 0.04);
        }
        if (anim.curIdleTicks < 1000) anim.curIdleTicks++;
      }

      const tgtOp = anim.curIdleTicks >= 75 ? 112 / 256 : 1;
      anim.curOpacity += (tgtOp - anim.curOpacity) * 0.25;
      if (Math.abs(tgtOp - anim.curOpacity) < 0.01) anim.curOpacity = tgtOp;
    }
  }

  /* 5-Detent Touchpad Sensitivity Slider (Mode 1) */
  const sens = clampTouchpadSensitivity(s.settings.touchpadSensitivity);
  const tgtPos = sens * 256;
  if (!anim.tpSliderDragging) {
    const diffPos = tgtPos - anim.tpSliderPos;
    if (diffPos !== 0 || anim.tpSliderVel !== 0) {
      const force = diffPos * 0.28;
      let vel = anim.tpSliderVel * 0.44 + force;
      if (Math.abs(vel) < 1 && diffPos !== 0) vel = diffPos > 0 ? 4 : -4;
      let nextPos = anim.tpSliderPos + vel;

      if (Math.abs(tgtPos - nextPos) <= 6 && Math.abs(vel) <= 14) {
        nextPos = tgtPos;
        vel = 0;
      }
      if (nextPos < -16) {
        nextPos = -16;
        vel = 0;
      }
      if (nextPos > 1040) {
        nextPos = 1040;
        vel = 0;
      }
      anim.tpSliderPos = nextPos;
      anim.tpSliderVel = vel;

      if (sens !== anim.tpNotchLast && Math.abs(tgtPos - nextPos) <= 44) {
        anim.tpNotchLast = sens;
        anim.tpDetentPulse = 1;
      }
    }
  }
  if (anim.tpDetentPulse > 0) {
    anim.tpDetentPulse = Math.max(0, anim.tpDetentPulse - 0.125);
  }

  /* 5-Detent Mouse Pointer Speed Slider (Mode 2) */
  const cspd = clampCursorSpeed(s.settings.cursorSpeed);
  const tgtSpdPos = cspd * 256;
  if (!anim.tpSpeedSliderDragging) {
    const diffPos = tgtSpdPos - anim.tpSpeedSliderPos;
    if (diffPos !== 0 || anim.tpSpeedSliderVel !== 0) {
      const force = diffPos * 0.28;
      let vel = anim.tpSpeedSliderVel * 0.44 + force;
      if (Math.abs(vel) < 1 && diffPos !== 0) vel = diffPos > 0 ? 4 : -4;
      let nextPos = anim.tpSpeedSliderPos + vel;

      if (Math.abs(tgtSpdPos - nextPos) <= 6 && Math.abs(vel) <= 14) {
        nextPos = tgtSpdPos;
        vel = 0;
      }
      if (nextPos < -16) {
        nextPos = -16;
        vel = 0;
      }
      if (nextPos > 1040) {
        nextPos = 1040;
        vel = 0;
      }
      anim.tpSpeedSliderPos = nextPos;
      anim.tpSpeedSliderVel = vel;

      if (cspd !== anim.tpSpeedNotchLast && Math.abs(tgtSpdPos - nextPos) <= 44) {
        anim.tpSpeedNotchLast = cspd;
        anim.tpSpeedDetentPulse = 1;
      }
    }
  }
  if (anim.tpSpeedDetentPulse > 0) {
    anim.tpSpeedDetentPulse = Math.max(0, anim.tpSpeedDetentPulse - 0.125);
  }

  /* Track layout changes (E) */
  if (s.settings.layout !== anim.lastLayout) {
    const prevIdx = LAYOUT_ORDER.indexOf(anim.lastLayout);
    const nextIdx = LAYOUT_ORDER.indexOf(s.settings.layout);
    anim.layoutFrom = anim.lastLayout;
    anim.lastLayout = s.settings.layout;
    anim.layoutTransProgress = 0.08;
    anim.valPulseProgress = 1;
    anim.valPulseDir = nextIdx >= prevIdx ? 1 : -1;
  }
  if (anim.layoutTransProgress < 1) {
    anim.layoutTransProgress += (1 - anim.layoutTransProgress) * 0.28 + 0.025;
    if (anim.layoutTransProgress >= 0.995) {
      anim.layoutTransProgress = 1;
      anim.layoutFrom = s.settings.layout;
    }
  }
  if (anim.valPulseProgress > 0) {
    anim.valPulseProgress = Math.max(0, anim.valPulseProgress - 0.14);
  }

  /* Track screen open / close / section transitions (A, B, G) */
  const curScreen = s.sui.screen;
  if (curScreen !== anim.lastUiScreen) {
    if (anim.lastUiScreen === 'LIBRARY' && curScreen !== 'LIBRARY') {
      anim.prevUiScreen = 'LIBRARY';
      anim.setOpenProgress = 0.16;
      anim.secTransProgress = 1;
    } else if (anim.lastUiScreen !== 'LIBRARY' && curScreen === 'LIBRARY') {
      anim.prevUiScreen = anim.lastUiScreen;
      anim.setOpenProgress = 0.84;
      anim.secTransProgress = 1;
    } else {
      anim.prevUiScreen = anim.lastUiScreen;
      anim.secTransDir = curScreen === 'SETTINGS_MAIN' ? -1 : 1;
      anim.secTransProgress = 0.12;
    }
    anim.lastUiScreen = curScreen;
    anim.lastCursorKey = '';
  }

  const targetOpen = curScreen !== 'LIBRARY' ? 1 : 0;
  if (anim.setOpenProgress !== targetOpen) {
    anim.setOpenProgress +=
      (targetOpen - anim.setOpenProgress) * 0.34 + (targetOpen > anim.setOpenProgress ? 0.02 : -0.02);
    if (Math.abs(targetOpen - anim.setOpenProgress) < 0.02) {
      anim.setOpenProgress = targetOpen;
    }
  }

  if (anim.secTransProgress < 1) {
    anim.secTransProgress += (1 - anim.secTransProgress) * 0.34 + 0.025;
    if (anim.secTransProgress >= 0.99) anim.secTransProgress = 1;
  }

  /* Track option selection & smooth list scroll (C) */
  const sui = s.sui;
  let cursor = 0;
  let scroll = 0;
  let rowY0 = 32;
  let stride = 30;
  if (curScreen === 'SETTINGS_MAIN') {
    cursor = sui.menuCursor;
    scroll = sui.menuScroll;
    rowY0 = 32;
    stride = 30;
  } else if (curScreen === 'SETTINGS_DISPLAY') {
    cursor = sui.displayCursor;
    scroll = sui.displayScroll;
    rowY0 = 30;
    stride = 30;
  } else if (curScreen === 'SETTINGS_TOUCHPAD') {
    cursor = sui.tpCursor;
    scroll = 0;
    rowY0 = 47;
    stride = 30;
  } else if (curScreen === 'SETTINGS_SHORTCUTS') {
    cursor = sui.scGameCursor;
    scroll = sui.scGameScroll;
    rowY0 = 32;
    stride = 30;
  } else if (curScreen === 'SETTINGS_KEY_HELP') {
    cursor = sui.subScroll;
    scroll = sui.subScroll;
    rowY0 = 33;
    stride = 26;
  } else if (curScreen === 'SETTINGS_SORT') {
    cursor = sui.subCursor;
    scroll = sui.subScroll;
    rowY0 = 32;
    stride = 30;
  } else if (curScreen === 'SETTINGS_STATS') {
    cursor = sui.subCursor;
    scroll = sui.subScroll;
    rowY0 = 33;
    stride = 36;
  } else if (curScreen === 'SETTINGS_ICONS') {
    cursor = Math.floor(sui.iconCursor / 3);
    scroll = sui.iconScrollRow;
    rowY0 = 33;
    stride = 90;
  } else if (curScreen === 'SETTINGS_PROFILES') {
    if (sui.profMode === 'LIST') {
      cursor = sui.profCursor;
      scroll = sui.profScroll;
      rowY0 = 32;
      stride = 30;
    } else if (sui.profMode === 'DETAIL' || sui.profMode === 'CONFIRM_DELETE') {
      cursor = sui.profDetailCursor;
      scroll = 0;
      rowY0 = 83;
      stride = 32;
    }
  }

  const cursorKey = `${curScreen}:${sui.profMode}:${cursor}:${scroll}`;
  const targetSelY = rowY0 + cursor * stride;
  const targetScrollPx = scroll * stride;

  if (anim.lastCursorKey === '') {
    anim.lastCursorKey = cursorKey;
    anim.prevCursor = cursor;
    anim.selY = targetSelY;
    anim.targetSelY = targetSelY;
    anim.listScrollPx = targetScrollPx;
    anim.targetScrollPx = targetScrollPx;
    anim.selGlowProgress = 0.25;
  } else if (cursorKey !== anim.lastCursorKey) {
    const prevParts = anim.lastCursorKey.split(':');
    const prevCurVal = Number(prevParts[2] || 0);
    anim.prevCursor = prevCurVal;
    anim.lastCursorKey = cursorKey;
    anim.targetSelY = targetSelY;
    anim.targetScrollPx = targetScrollPx;
    anim.selGlowProgress = 0.22;
  }

  anim.selY += (anim.targetSelY - anim.selY) * 0.38;
  if (Math.abs(anim.targetSelY - anim.selY) < 0.35) anim.selY = anim.targetSelY;

  anim.listScrollPx += (anim.targetScrollPx - anim.listScrollPx) * 0.36;
  if (Math.abs(anim.targetScrollPx - anim.listScrollPx) < 0.35) {
    anim.listScrollPx = anim.targetScrollPx;
  }

  if (anim.selGlowProgress < 1) {
    anim.selGlowProgress = Math.min(1, anim.selGlowProgress + 0.16);
  }

  /* Smooth gliding key-picker highlight in SETTINGS_SHORTCUTS */
  if (curScreen === 'SETTINGS_SHORTCUTS' && sui.scPickingKey) {
    const k = sui.scKeyCursor;
    const r = Math.floor(k / 8);
    const c = k % 8;
    const tgtX = 34 + c * 31;
    const tgtY = 72 + r * 26;
    const tgtW = k === 0 ? 28 : 26;
    if (anim.lastScPickKey < 0) {
      anim.scKeyX = tgtX;
      anim.scKeyY = tgtY;
      anim.scKeyW = tgtW;
      anim.lastScPickKey = k;
    } else {
      anim.scKeyX += (tgtX - anim.scKeyX) * 0.44;
      anim.scKeyY += (tgtY - anim.scKeyY) * 0.44;
      anim.scKeyW += (tgtW - anim.scKeyW) * 0.44;
      if (Math.abs(tgtX - anim.scKeyX) < 0.35) anim.scKeyX = tgtX;
      if (Math.abs(tgtY - anim.scKeyY) < 0.35) anim.scKeyY = tgtY;
      if (Math.abs(tgtW - anim.scKeyW) < 0.35) anim.scKeyW = tgtW;
      anim.lastScPickKey = k;
    }
  } else {
    anim.lastScPickKey = -1;
  }

  /* Quick Profile Selector overlay animation + gliding multicolor ring + 4-stage profile transitions */
  const curSwitchSeq = s.profSwitchSeq ?? 0;
  if (curSwitchSeq !== anim.lastProfSwitchSeq) {
    anim.lastProfSwitchSeq = curSwitchSeq;
    anim.profSwitchProgress = 0;
    anim.profSwitchAvatarId = s.profSwitchAvatarId ?? getActiveProfile(s).avatarId;
    anim.profSwitchOriginX = s.profSwitchOriginX ?? 160;
    anim.profSwitchOriginY = s.profSwitchOriginY ?? 114;
    anim.profSwitchOriginD = s.profSwitchOriginD ?? 36;
  }
  if (anim.profSwitchProgress < 1) {
    anim.qpGlowPhase = (anim.qpGlowPhase + 0.14) % (Math.PI * 2);
    anim.profSwitchProgress = Math.min(1, anim.profSwitchProgress + 1 / 22);
  }

  const curPlusSeq = s.profPlusSeq ?? 0;
  if (curPlusSeq !== anim.lastProfPlusSeq) {
    anim.lastProfPlusSeq = curPlusSeq;
    anim.profPlusProgress = 0;
    anim.profPlusOriginX = s.profPlusOriginX ?? 160;
    anim.profPlusOriginY = s.profPlusOriginY ?? 114;
  }
  if (anim.profPlusProgress < 1) {
    anim.qpGlowPhase = (anim.qpGlowPhase + 0.14) % (Math.PI * 2);
    anim.profPlusProgress = Math.min(1, anim.profPlusProgress + 1 / 18);
  }

  const curBirthSeq = s.profBirthSeq ?? 0;
  if (curBirthSeq !== anim.lastProfBirthSeq) {
    anim.lastProfBirthSeq = curBirthSeq;
    anim.profBirthProgress = 0;
    anim.profBirthAvatarId = s.profBirthAvatarId ?? getActiveProfile(s).avatarId;
    anim.profBirthSlot = s.profBirthSlot ?? s.profiles.activeIndex;
  }
  if (anim.profBirthProgress < 1) {
    anim.qpGlowPhase = (anim.qpGlowPhase + 0.14) % (Math.PI * 2);
    anim.profBirthProgress = Math.min(1, anim.profBirthProgress + 1 / 20);
  }

  const targetQp = curScreen === 'LIBRARY' && sui.qpOpen ? 1 : 0;
  anim.qpOpenAnim += (targetQp - anim.qpOpenAnim) * 0.34;
  if (Math.abs(targetQp - anim.qpOpenAnim) < 0.01) anim.qpOpenAnim = targetQp;
  if (
    sui.qpOpen ||
    (curScreen === 'SETTINGS_PROFILES' &&
      (sui.profMode === 'EDIT_AVATAR' || sui.profMode === 'CREATE_AVATAR'))
  ) {
    anim.qpGlowPhase = (anim.qpGlowPhase + 0.09) % (Math.PI * 2);
  }
  if (sui.qpOpen || anim.qpOpenAnim > 0.01) {
    const pcount = Math.max(1, s.profiles.items.length);
    const totalSlots = pcount + 1;
    const visMax = 6;
    const diam = 34;
    const stride = 44;
    const cur = Math.min(totalSlots - 1, Math.max(0, sui.qpCursor));
    if (cur !== anim.lastQpCursor) {
      anim.lastQpCursor = cur;
      anim.qpSelBounce = 1;
    }
    if (anim.qpSelBounce > 0) {
      anim.qpSelBounce = Math.max(0, anim.qpSelBounce - 0.15);
    }
    const tgtPlus = cur === pcount ? 1 : 0;
    anim.qpPlusFocus += (tgtPlus - anim.qpPlusFocus) * 0.38;
    if (Math.abs(tgtPlus - anim.qpPlusFocus) < 0.015) anim.qpPlusFocus = tgtPlus;

    for (let slot = 0; slot < 9; slot++) {
      const tgtScale = slot === cur ? 1.06 : 0.88;
      anim.qpSlotScales[slot] += (tgtScale - anim.qpSlotScales[slot]) * 0.42;
      if (Math.abs(tgtScale - anim.qpSlotScales[slot]) < 0.008) {
        anim.qpSlotScales[slot] = tgtScale;
      }
    }

    const shown = Math.min(visMax, totalSlots);
    let winStart = 0;
    if (totalSlots > visMax) {
      winStart = cur - Math.floor(visMax / 2);
      if (winStart < 0) winStart = 0;
      if (winStart > totalSlots - visMax) winStart = totalSlots - visMax;
    }
    const stripW = shown * diam + (shown - 1) * (stride - diam);
    const startX = 160 - Math.floor(stripW / 2);
    const tgtGx = startX + (cur - winStart) * stride + diam / 2;
    if (anim.qpGlowX <= 0) {
      anim.qpGlowX = tgtGx;
      anim.qpTrailX = tgtGx;
    } else {
      anim.qpGlowX += (tgtGx - anim.qpGlowX) * 0.48;
      anim.qpTrailX += (anim.qpGlowX - anim.qpTrailX) * 0.28;
    }
    if (Math.abs(tgtGx - anim.qpGlowX) < 0.35) anim.qpGlowX = tgtGx;
    if (Math.abs(anim.qpGlowX - anim.qpTrailX) < 0.35) anim.qpTrailX = anim.qpGlowX;
    anim.qpGlowY = 114;
  } else {
    anim.qpGlowX = 0;
    anim.qpTrailX = 0;
  }

  /* Library inline search bar & category sliding pill + content slide */
  const targetSearchBar = sui.searchActive ? 1 : 0;
  anim.searchBarAnim += (targetSearchBar - anim.searchBarAnim) * 0.36;
  if (Math.abs(targetSearchBar - anim.searchBarAnim) < 0.015) {
    anim.searchBarAnim = targetSearchBar;
  }

  if (s.activeCategory !== anim.lastCategory) {
    const prevIdx = CATEGORY_ORDER.indexOf(anim.lastCategory);
    const nextIdx = CATEGORY_ORDER.indexOf(s.activeCategory);
    anim.catTransDir = nextIdx === (prevIdx + 1) % CATEGORY_ORDER.length ? 1 : -1;
    anim.catTransProgress = 0.18;
    anim.lastCategory = s.activeCategory;
  }
  if (anim.catTransProgress < 1) {
    anim.catTransProgress += (1 - anim.catTransProgress) * 0.36 + 0.02;
    if (anim.catTransProgress >= 0.99) anim.catTransProgress = 1;
  }

  const tgtCatX =
    s.activeCategory === 'ALL' ? 34 : s.activeCategory === 'FAVORITES' ? 76 : 112;
  const tgtCatW = s.activeCategory === 'ALL' ? 36 : 30;
  anim.catIndX += (tgtCatX - anim.catIndX) * 0.38;
  anim.catIndW += (tgtCatW - anim.catIndW) * 0.38;
  if (Math.abs(tgtCatX - anim.catIndX) < 0.35) anim.catIndX = tgtCatX;
  if (Math.abs(tgtCatW - anim.catIndW) < 0.35) anim.catIndW = tgtCatW;

  /* Track modal fade & button glide (F) */
  const modalOpen =
    curScreen === 'SETTINGS_RESET' ||
    (curScreen === 'SETTINGS_SHORTCUTS' && (sui.scPickingKey || sui.scConflictPrompt)) ||
    (curScreen === 'SETTINGS_ICONS' && sui.iconConfirmModal) ||
    (curScreen === 'SETTINGS_PROFILES' && sui.profMode === 'CONFIRM_DELETE');
  if (modalOpen && !anim.lastModalOpen) {
    anim.modalProgress = 0.14;
  }
  anim.lastModalOpen = modalOpen;
  const targetModal = modalOpen ? 1 : 0;
  anim.modalProgress += (targetModal - anim.modalProgress) * 0.36;
  if (Math.abs(targetModal - anim.modalProgress) < 0.015) anim.modalProgress = targetModal;

  const confirmYes =
    curScreen === 'SETTINGS_RESET'
      ? sui.resetConfirmYes
      : curScreen === 'SETTINGS_SHORTCUTS' && sui.scConflictPrompt
      ? sui.scConflictConfirmYes
      : curScreen === 'SETTINGS_ICONS' && sui.iconConfirmModal
      ? sui.iconConfirmYes
      : curScreen === 'SETTINGS_PROFILES' && sui.profMode === 'CONFIRM_DELETE'
      ? sui.profDeleteYes
      : false;
  const targetBtn = confirmYes ? 1 : 0;
  anim.modalBtnProgress += (targetBtn - anim.modalBtnProgress) * 0.36;
  if (Math.abs(targetBtn - anim.modalBtnProgress) < 0.015) anim.modalBtnProgress = targetBtn;
}

function drawLibraryGridForLayout(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  layout: LayoutMode,
  nameT: number,
  scT: number,
  scrollY: number,
  vSettled: boolean,
  searchH: number,
  catDx: number,
  cardScales: Map<string, number>,
  cardOffsetX: Map<string, number>,
  cardOffsetY: Map<string, number>,
  allowHighlight: boolean,
  theme: ConsoleThemePalette,
  imgCache: Map<string, HTMLImageElement>,
  launchSkipGameIdx = -1,
  otherCardsAlpha = 1
) {
  const itemCount = s.filteredIndices.length;
  const m00 = getLayoutMetrics(layout, false, false, itemCount);
  const m01 = getLayoutMetrics(layout, false, true, itemCount);
  const m10 = getLayoutMetrics(layout, true, false, itemCount);
  const m11 = getLayoutMetrics(layout, true, true, itemCount);

  const bilerp = (v00: number, v01: number, v10: number, v11: number) => {
    const a = v00 + (v01 - v00) * scT;
    const b = v10 + (v11 - v10) * scT;
    return Math.round(a + (b - a) * nameT);
  };

  const cols = m11.cols;
  const rows = m11.rows;
  const perPage = m11.perPage;
  const cardW = bilerp(m00.cardW, m01.cardW, m10.cardW, m11.cardW);
  const cardH = bilerp(m00.cardH, m01.cardH, m10.cardH, m11.cardH);
  const gapX = bilerp(m00.gapX, m01.gapX, m10.gapX, m11.gapX);
  const gapY = bilerp(m00.gapY, m01.gapY, m10.gapY, m11.gapY);
  const marginX = bilerp(m00.marginX, m01.marginX, m10.marginX, m11.marginX);
  const topY = bilerp(m00.topY, m01.topY, m10.topY, m11.topY);
  const rowStrideY = bilerp(m00.rowStrideY, m01.rowStrideY, m10.rowStrideY, m11.rowStrideY);
  const pageStrideY = bilerp(m00.pageStrideY, m01.pageStrideY, m10.pageStrideY, m11.pageStrideY);
  const viewBottom = bilerp(m00.viewBottom, m01.viewBottom, m10.viewBottom, m11.viewBottom);
  const paginationY = bilerp(m00.paginationY, m01.paginationY, m10.paginationY, m11.paginationY);
  const labelH = bilerp(m00.labelH, m01.labelH, m10.labelH, m11.labelH);
  const radius = m11.radius;
  const clipTop = NL_VIEW_TOP + searchH;
  const mInterp = {
    cols,
    rows,
    perPage,
    cardW,
    cardH,
    radius,
    marginX,
    gapX,
    topY,
    gapY,
    labelH,
    rowStrideY,
    pageStrideY,
    viewBottom,
    paginationY,
  };

  const selGameIdx = s.filteredIndices[s.selectedPos];

  for (let pass = 0; pass < 2; pass++) {
    for (let fIdx = 0; fIdx < s.filteredIndices.length; fIdx++) {
      const gameIdx = s.filteredIndices[fIdx];
      if (launchSkipGameIdx >= 0 && gameIdx === launchSkipGameIdx) continue;
      const isSelPass = pass === 1;
      if ((gameIdx === selGameIdx) !== isSelPass) continue;
      if (!isSelPass && otherCardsAlpha <= 0.005) continue;

      const g = s.games[gameIdx];
      const row = Math.floor(fIdx / cols);

      const glideDx = launchSkipGameIdx < 0 ? Math.round(cardOffsetX.get(g.id) ?? 0) : 0;
      const glideDy = launchSkipGameIdx < 0 ? Math.round(cardOffsetY.get(g.id) ?? 0) : 0;
      const slotCoords = computeGridSlotPos(mInterp, itemCount, fIdx);
      const baseX = slotCoords.x + catDx + glideDx;
      const baseY = slotCoords.y + searchH - scrollY + glideDy;

      if (baseY + cardH + labelH + 12 < clipTop || baseY - 12 >= viewBottom) {
        continue;
      }

      const scale = cardScales.get(g.id) ?? NL_SCALE_NORMAL;
      const drawW = cardW * scale;
      const drawH = cardH * scale;
      const drawX = baseX - (drawW - cardW) / 2;
      let drawY = baseY - (drawH - cardH) / 2;

      /* Clamp row-0 only when vertical scroll has settled (prevents upward pagination overlap) */
      if (vSettled && searchH === 0 && drawY < NL_VIEW_TOP && row === s.currentPage * rows) {
        drawY = NL_VIEW_TOP + 1;
      }

      const cardAlpha = isSelPass ? 1 : Math.max(0, Math.min(1, otherCardsAlpha));
      ctx.save();
      if (cardAlpha < 0.999) {
        ctx.globalAlpha *= cardAlpha;
      }
      drawGameCoverCard(
        ctx,
        drawX,
        drawY,
        drawW,
        drawH,
        radius,
        g,
        isSelPass && scale > 1.005 && cardAlpha >= 0.99,
        computeNavHighlightStrength(scale, !allowHighlight || cardAlpha < 0.99),
        theme,
        imgCache
      );
      if (
        isMouseModeActive(s) &&
        s.sui.hoverValid &&
        s.sui.hoverKind === 'GAME_CARD' &&
        s.sui.hoverIndex === fIdx &&
        cardAlpha >= 0.99
      ) {
        traceRoundedRect(ctx, drawX - 1, drawY - 1, drawW + 2, drawH + 2, radius + 1);
        ctx.strokeStyle = theme.accentSecondary;
        ctx.lineWidth = 1.25;
        ctx.stroke();
      }
      ctx.restore();

      if (nameT > 0.03 && labelH > 0 && cardAlpha > 0.01) {
        ctx.save();
        ctx.globalAlpha *= nameT * cardAlpha;
        ctx.font =
          cols >= 4
            ? 'bold 6.5px "JetBrains Mono", monospace'
            : cols === 3 && cardW < 64
            ? 'bold 7px "JetBrains Mono", monospace'
            : 'bold 7.5px "JetBrains Mono", monospace';
        ctx.textBaseline = 'top';
        const labelMaxW = cols === 1 ? cardW + 48 : cardW + (gapX > 8 ? gapX - 6 : 2);
        let disp = g.title;
        if (ctx.measureText(disp).width > labelMaxW) {
          while (disp.length > 1 && ctx.measureText(disp + '..').width > labelMaxW) {
            disp = disp.slice(0, -1);
          }
          disp += '..';
        }
        const tw = ctx.measureText(disp).width;
        const lx = Math.max(2, Math.round(drawX + (drawW - tw) / 2));
        const ly = Math.round(drawY + drawH + 1);
        ctx.fillStyle = isSelPass ? theme.textPrimary : theme.textSecondary;
        ctx.fillText(disp, lx, ly);
        ctx.restore();
      }
    }
  }
}

function compositeSettingsLiquidDrop(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  anim: SettingsAnimState,
  theme: ConsoleThemePalette,
  activeSettingsScreen: SettingsScreenId
) {
  if (!anim.setDropCanvas) {
    anim.setDropCanvas = document.createElement('canvas');
    anim.setDropCanvas.width = NL_SCREEN_W;
    anim.setDropCanvas.height = NL_SCREEN_H;
  }
  const dropCtx = anim.setDropCanvas.getContext('2d');
  if (!dropCtx) return;
  dropCtx.imageSmoothingEnabled = false;
  drawSettingsView(dropCtx, s, anim, theme, activeSettingsScreen);

  const libImg = ctx.getImageData(0, 0, NL_SCREEN_W, NL_SCREEN_H);
  const libData = libImg.data;
  const setData = dropCtx.getImageData(0, 0, NL_SCREEN_W, NL_SCREEN_H).data;

  const t = Math.max(0, Math.min(1, anim.setOpenProgress));
  const inv = 1 - t;
  const cubic = 1 - inv * inv * inv;
  const ease = t * 0.3 + cubic * 0.7;

  const edgeW = 24;
  const front = (NL_SETTINGS_BLOB_DATA.maxVal + edgeW + 4) * ease;
  const bgDim = Math.min(0.32, ease * 0.25);
  const [dr, dg, db] = theme.bgMainRgb;
  const [rr, rg, rb] = hexToRgbTuple(theme.accentSecondary);
  const field = NL_SETTINGS_BLOB_DATA.field;
  const totalPx = NL_SCREEN_W * NL_SCREEN_H;

  for (let p = 0; p < totalPx; p++) {
    const delta = front - field[p];
    const idx = p << 2;
    if (delta >= edgeW) {
      libData[idx] = setData[idx];
      libData[idx + 1] = setData[idx + 1];
      libData[idx + 2] = setData[idx + 2];
      libData[idx + 3] = 255;
    } else if (delta <= 0) {
      if (bgDim > 0) {
        libData[idx] = Math.round(libData[idx] + (dr - libData[idx]) * bgDim);
        libData[idx + 1] = Math.round(libData[idx + 1] + (dg - libData[idx + 1]) * bgDim);
        libData[idx + 2] = Math.round(libData[idx + 2] + (db - libData[idx + 2]) * bgDim);
      }
    } else {
      const br = bgDim > 0 ? libData[idx] + (dr - libData[idx]) * bgDim : libData[idx];
      const bg = bgDim > 0 ? libData[idx + 1] + (dg - libData[idx + 1]) * bgDim : libData[idx + 1];
      const bb = bgDim > 0 ? libData[idx + 2] + (db - libData[idx + 2]) * bgDim : libData[idx + 2];

      const a = delta / edgeW;
      let outR = br + (setData[idx] - br) * a;
      let outG = bg + (setData[idx + 1] - bg) * a;
      let outB = bb + (setData[idx + 2] - bb) * a;

      const mid = edgeW / 2;
      const distMid = Math.abs(delta - mid);
      const glowA = Math.max(0, ((mid - distMid) / mid) * 0.31);
      if (glowA > 0) {
        outR += (rr - outR) * glowA;
        outG += (rg - outG) * glowA;
        outB += (rb - outB) * glowA;
      }

      libData[idx] = Math.round(outR);
      libData[idx + 1] = Math.round(outG);
      libData[idx + 2] = Math.round(outB);
      libData[idx + 3] = 255;
    }
  }

  ctx.putImageData(libImg, 0, 0);
}

function drawLauncherView(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  anim: SettingsAnimState & {
    scrollY: number;
    dotPos: number;
    cardScales: Map<string, number>;
    cardOffsetX: Map<string, number>;
    cardOffsetY: Map<string, number>;
    lastFilteredOrder: number[];
    toastStartMs: number;
    activeToastText: string | null;
  },
  nowMs: number,
  imgCache: Map<string, HTMLImageElement>,
  allowHighlight: boolean,
  launchSkipGameIdx = -1,
  otherCardsAlpha = 1
) {
  const theme = getConsoleTheme(s.displayMode, s.accentColor);
  const prevCatBeforeStep = anim.lastCategory;
  stepSettingsAnimations(s, anim);
  if (s.activeCategory !== prevCatBeforeStep) {
    anim.scrollY = 0;
    anim.cardOffsetX.clear();
    anim.cardOffsetY.clear();
  }

  const isSettingsOpenOrClosing =
    s.sui.screen !== 'LIBRARY' || (anim.setOpenProgress > 0.01 && anim.prevUiScreen !== 'LIBRARY');

  if (s.sui.screen !== 'LIBRARY' && anim.setOpenProgress >= 0.995) {
    drawSettingsView(ctx, s, anim, theme, s.sui.screen);
    if (anim.profPlusProgress < 1) {
      drawProfilePlusTransition(ctx, anim, theme);
    }
    if (anim.profSwitchProgress < 1) {
      drawProfileSwitchTransition(ctx, s, anim, theme, imgCache);
    }
    drawTouchpadGraphicalCursor(ctx, s, anim, theme);
    renderToastBanner(ctx, anim, nowMs, theme);
    return;
  }

  const itemCount = s.filteredIndices.length;
  const mOff = getLayoutMetrics(s.settings.layout, s.settings.showNames, false, itemCount);
  const mOn = getLayoutMetrics(s.settings.layout, s.settings.showNames, true, itemCount);
  const scT = anim.switchShortcutsAnim;
  const nameT = anim.switchShowNamesAnim;
  const m = {
    ...mOn,
    cardW: Math.round(mOff.cardW + (mOn.cardW - mOff.cardW) * scT),
    cardH: Math.round(mOff.cardH + (mOn.cardH - mOff.cardH) * scT),
    gapX: Math.round(mOff.gapX + (mOn.gapX - mOff.gapX) * scT),
    gapY: Math.round(mOff.gapY + (mOn.gapY - mOff.gapY) * scT),
    marginX: Math.round(mOff.marginX + (mOn.marginX - mOff.marginX) * scT),
    topY: Math.round(mOff.topY + (mOn.topY - mOff.topY) * scT),
    rowStrideY: Math.round(mOff.rowStrideY + (mOn.rowStrideY - mOff.rowStrideY) * scT),
    pageStrideY: Math.round(mOff.pageStrideY + (mOn.pageStrideY - mOff.pageStrideY) * scT),
    viewBottom: Math.round(mOff.viewBottom + (mOn.viewBottom - mOff.viewBottom) * scT),
    paginationY: Math.round(mOff.paginationY + (mOn.paginationY - mOff.paginationY) * scT),
  };

  /* Detect post-return sort order updates when in Library (never during launch transition) */
  if (launchSkipGameIdx < 0 && s.runningGameIndex === null) {
    const prevOrder = anim.lastFilteredOrder;
    if (
      prevOrder.length === s.filteredIndices.length &&
      s.activeCategory === prevCatBeforeStep &&
      !s.sui.searchActive
    ) {
      let orderChanged = false;
      for (let i = 0; i < s.filteredIndices.length; i++) {
        if (prevOrder[i] !== s.filteredIndices[i]) {
          orderChanged = true;
          break;
        }
      }
      if (orderChanged) {
        for (let newF = 0; newF < s.filteredIndices.length; newF++) {
          const gIdx = s.filteredIndices[newF];
          const oldF = prevOrder.indexOf(gIdx);
          const gId = s.games[gIdx]?.id;
          if (gId && oldF >= 0 && oldF !== newF) {
            const oldPos = computeGridSlotPos(m, s.filteredIndices.length, oldF);
            const newPos = computeGridSlotPos(m, s.filteredIndices.length, newF);
            const dxPx = oldPos.x - newPos.x;
            const dyPx = oldPos.y - newPos.y;
            anim.cardOffsetX.set(gId, dxPx);
            anim.cardOffsetY.set(gId, dyPx);
          }
        }
      }
    }
    anim.lastFilteredOrder = [...s.filteredIndices];

    /* Smoothly step post-return sort slot glide offsets toward 0 */
    for (const [gId, dx] of anim.cardOffsetX.entries()) {
      const nextDx = dx * 0.6875;
      if (Math.abs(nextDx) < 0.35) {
        anim.cardOffsetX.delete(gId);
      } else {
        anim.cardOffsetX.set(gId, nextDx);
      }
    }
    for (const [gId, dy] of anim.cardOffsetY.entries()) {
      const nextDy = dy * 0.6875;
      if (Math.abs(nextDy) < 0.35) {
        anim.cardOffsetY.delete(gId);
      } else {
        anim.cardOffsetY.set(gId, nextDy);
      }
    }
  }

  /* Smooth vertical scroll & dot interpolation (interruptible at 60 FPS) */
  const targetScrollY = s.currentPage * m.pageStrideY;
  anim.scrollY += (targetScrollY - anim.scrollY) * 0.28;
  if (Math.abs(targetScrollY - anim.scrollY) < 0.35) anim.scrollY = targetScrollY;
  const vSettled = Math.abs(targetScrollY - anim.scrollY) <= 1.0;

  const targetDot = s.currentPage;
  anim.dotPos += (targetDot - anim.dotPos) * 0.3;
  if (Math.abs(targetDot - anim.dotPos) < 0.02) anim.dotPos = targetDot;

  const selGameId =
    s.filteredIndices.length > 0 ? s.games[s.filteredIndices[s.selectedPos]]?.id : null;

  for (const g of s.games) {
    const cur = anim.cardScales.get(g.id) ?? NL_SCALE_NORMAL;
    const isSel = g.id === selGameId;
    const tgt = isSel
      ? s.launchHolding
        ? 232 / 256
        : NL_SCALE_SELECTED
      : NL_SCALE_NORMAL;
    let next = cur + (tgt - cur) * 0.38;
    if (Math.abs(tgt - next) < 0.002) next = tgt;
    anim.cardScales.set(g.id, next);
  }

  /* 1. Clear 320x240 background */
  ctx.fillStyle = theme.bgMain;
  ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);

  /* 2. Top Header Bar (y = 0..25): Emblem + smoothly sliding category capsule + rotating gear */
  ctx.fillStyle = theme.bgTopBar;
  ctx.fillRect(0, 0, NL_SCREEN_W, NL_TOP_BAR_H);
  ctx.fillStyle = theme.borderSubtle;
  ctx.fillRect(0, NL_TOP_BAR_H, NL_SCREEN_W, 1);

  const mAct = isMouseModeActive(s) && s.sui.hoverValid;
  {
    const activeProf = getActiveProfile(s);
    const hovP = mAct && s.sui.hoverKind === 'PROFILE_BADGE';
    ctx.beginPath();
    ctx.arc(17.5, 12.5, hovP ? 9.5 : 8.5, 0, Math.PI * 2);
    ctx.fillStyle = hovP ? theme.accentSecondary : theme.accentPrimary;
    ctx.fill();
    drawCircularAvatar(ctx, 10, 5, 15, activeProf.avatarId, false, theme, imgCache);
  }

  const tabMeta: { id: CategoryId; x: number; w: number; label: string }[] = [
    { id: 'ALL', x: 34, w: 36, label: CATEGORY_LABELS.ALL },
    { id: 'FAVORITES', x: 76, w: 30, label: CATEGORY_LABELS.FAVORITES },
    { id: 'RECENT', x: 112, w: 30, label: CATEGORY_LABELS.RECENT },
  ];
  tabMeta.forEach((t, cIdx) => {
    traceRoundedRect(ctx, t.x, 5, t.w, 15, 4);
    ctx.fillStyle = theme.pillBg;
    ctx.fill();
    if (
      mAct &&
      s.sui.hoverKind === 'CATEGORY' &&
      s.sui.hoverIndex === cIdx &&
      t.id !== s.activeCategory
    ) {
      ctx.strokeStyle = theme.accentSecondary;
      ctx.lineWidth = 1;
      ctx.stroke();
    }
  });

  const indX = anim.catIndX;
  const indW = anim.catIndW;
  traceRoundedRect(ctx, Math.round(indX), 5, Math.round(indW), 15, 4);
  ctx.fillStyle = theme.accentPrimary;
  ctx.fill();

  ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
  ctx.textBaseline = 'middle';
  for (const t of tabMeta) {
    const ov = Math.max(0, Math.min(t.x + t.w, indX + indW) - Math.max(t.x, indX));
    const ratio = Math.max(0, Math.min(1, ov / t.w));
    ctx.fillStyle = lerpHexColor(theme.textSecondary, theme.textOnAccent, ratio);
    ctx.fillText(t.label, t.x + 6, 12.8);
  }

  /* Top-Bar Favorite Star Toggle Button (x=148..164, y=5..20) */
  {
    const selGame =
      s.filteredIndices.length > 0 && s.selectedPos >= 0 && s.selectedPos < s.filteredIndices.length
        ? s.games[s.filteredIndices[s.selectedPos]]
        : null;
    const selIsFav = Boolean(selGame?.isFavorite);
    const hovFav = mAct && s.sui.hoverKind === 'FAV_BTN';
    traceRoundedRect(ctx, 148, 5, 16, 15, 4);
    ctx.fillStyle = selIsFav ? theme.cardBgSel : theme.pillBg;
    ctx.fill();
    if (hovFav) {
      ctx.strokeStyle = theme.accentSecondary;
      ctx.lineWidth = 1;
      ctx.stroke();
    }
    ctx.font = 'bold 9px "JetBrains Mono", monospace';
    ctx.fillStyle = selIsFav ? '#fbbf24' : theme.textSecondary;
    ctx.fillText('★', 151.8, 12.8);
  }

  const gearOpenEase =
    anim.setOpenProgress <= 0
      ? 0
      : (() => {
          const t = Math.max(0, Math.min(1, anim.setOpenProgress));
          const inv = 1 - t;
          return t * 0.3 + (1 - inv * inv * inv) * 0.7;
        })();
  if (mAct && s.sui.hoverKind === 'SETTINGS_BTN') {
    ctx.beginPath();
    ctx.arc(
      NL_GEAR_BTN_X + NL_GEAR_BTN_D / 2,
      NL_GEAR_BTN_Y + NL_GEAR_BTN_D / 2,
      NL_GEAR_BTN_D / 2 + 2,
      0,
      Math.PI * 2
    );
    ctx.fillStyle = theme.accentSecondary;
    ctx.fill();
  }
  drawGearButton(
    ctx,
    NL_GEAR_BTN_X,
    NL_GEAR_BTN_Y,
    NL_GEAR_BTN_D,
    theme,
    false,
    gearOpenEase * Math.PI * 0.75
  );

  /* 3. Inline Library Search Bar + Clipped Vertical Library Viewport */
  const searchH = Math.round(28 * anim.searchBarAnim);
  const gridClipTop = Math.min(m.viewBottom, NL_VIEW_TOP + searchH);

  let catDx = 0;
  if (anim.catTransProgress < 1) {
    const inv = 1 - Math.max(0, Math.min(1, anim.catTransProgress));
    const ease = 1 - inv * inv * inv;
    catDx = Math.round(anim.catTransDir * (1 - ease) * 18);
  }

  if (searchH > 4) {
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, NL_VIEW_TOP, NL_SCREEN_W, m.viewBottom - NL_VIEW_TOP);
    ctx.clip();

    const sbY = NL_VIEW_TOP + 2 + searchH - 28;
    traceRoundedRect(ctx, 10, sbY, 300, 22, 6);
    ctx.fillStyle = theme.accentPrimary;
    ctx.fill();
    traceRoundedRect(ctx, 11, sbY + 1, 298, 20, 5);
    ctx.fillStyle = theme.bgTopBar;
    ctx.fill();

    drawSettingsIcon11x11(ctx, 17, sbY + 5, 'SEARCH', theme.accentSecondary);

    ctx.font = 'bold 8px "JetBrains Mono", monospace';
    ctx.textBaseline = 'middle';
    if (s.sui.searchQuery.length > 0) {
      drawEllipsisText(ctx, 33, sbY + 11.5, `${s.sui.searchQuery}_`, 198, theme.textPrimary);
    } else {
      ctx.fillStyle = theme.textSecondary;
      ctx.fillText('Recherche...', 33, sbY + 11.5);
    }

    const cntStr = String(s.filteredIndices.length);
    const cw = Math.ceil(ctx.measureText(cntStr).width);
    traceRoundedRect(ctx, 278 - cw - 8, sbY + 4, cw + 8, 14, 4);
    ctx.fillStyle = theme.pillBg;
    ctx.fill();
    ctx.fillStyle = theme.accentSecondary;
    ctx.fillText(cntStr, 278 - cw - 4, sbY + 11.5);

    ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
    ctx.fillStyle = theme.textSecondary;
    ctx.fillText('ESC', 284, sbY + 11.5);
    ctx.restore();
  }

  ctx.save();
  ctx.beginPath();
  ctx.rect(0, gridClipTop, NL_SCREEN_W, Math.max(0, m.viewBottom - gridClipTop));
  ctx.clip();

  if (s.filteredIndices.length > 0) {
    const scrollY = Math.round(anim.scrollY);
    if (anim.layoutTransProgress < 0.99 && anim.layoutFrom !== s.settings.layout) {
      const ease = 1 - Math.pow(1 - Math.max(0, Math.min(1, anim.layoutTransProgress)), 3);
      ctx.save();
      ctx.globalAlpha = 1 - ease;
      drawLibraryGridForLayout(
        ctx,
        s,
        anim.layoutFrom,
        nameT,
        scT,
        scrollY,
        vSettled,
        searchH,
        catDx,
        anim.cardScales,
        anim.cardOffsetX,
        anim.cardOffsetY,
        allowHighlight,
        theme,
        imgCache,
        launchSkipGameIdx,
        otherCardsAlpha
      );
      ctx.globalAlpha = ease;
      drawLibraryGridForLayout(
        ctx,
        s,
        s.settings.layout,
        nameT,
        scT,
        scrollY,
        vSettled,
        searchH,
        catDx,
        anim.cardScales,
        anim.cardOffsetX,
        anim.cardOffsetY,
        allowHighlight,
        theme,
        imgCache,
        launchSkipGameIdx,
        otherCardsAlpha
      );
      ctx.restore();
    } else {
      drawLibraryGridForLayout(
        ctx,
        s,
        s.settings.layout,
        nameT,
        scT,
        scrollY,
        vSettled,
        searchH,
        catDx,
        anim.cardScales,
        anim.cardOffsetX,
        anim.cardOffsetY,
        allowHighlight,
        theme,
        imgCache,
        launchSkipGameIdx,
        otherCardsAlpha
      );
    }

    const [br, bg, bb] = theme.bgMainRgb;
    const fadeGrad = ctx.createLinearGradient(0, m.viewBottom - 6, 0, m.viewBottom);
    fadeGrad.addColorStop(0, `rgba(${br}, ${bg}, ${bb}, 0)`);
    fadeGrad.addColorStop(1, theme.bgMain);
    ctx.fillStyle = fadeGrad;
    ctx.fillRect(0, m.viewBottom - 6, NL_SCREEN_W, 6);
  } else {
    const emptyY = Math.round((gridClipTop + m.viewBottom - 48) / 2);
    traceRoundedRect(ctx, 48 + catDx, emptyY, 224, 48, 7);
    ctx.fillStyle = theme.cardBg;
    ctx.fill();
    const msg = s.sui.searchActive
      ? 'Aucun jeu trouve (ESC)'
      : s.activeCategory === 'FAVORITES'
      ? 'AUCUN FAVORI (VAR)'
      : 'AUCUN JEU RECENT';
    ctx.fillStyle = theme.textSecondary;
    ctx.font = 'bold 8px "JetBrains Mono", monospace';
    ctx.textBaseline = 'middle';
    ctx.fillText(msg, Math.round((NL_SCREEN_W - msg.length * 5) / 2) + catDx, emptyY + 24);
  }
  ctx.restore();

  /* 4. Smart Adaptive Pagination: circular dots (<= 19) or compact horizontal dashes (> 19) */
  const indicators = computePaginationIndicators(
    s.currentPage,
    s.totalPages,
    anim.dotPos,
    m.paginationY
  );
  let pSx = NL_SCREEN_W;
  let pEx = 0;
  indicators.forEach((ind, iIdx) => {
    if (ind.x < pSx) pSx = ind.x;
    if (ind.x + ind.width > pEx) pEx = ind.x + ind.width;
    const hovDot = mAct && s.sui.hoverKind === 'PAGE_DOT' && s.sui.hoverIndex === iIdx;
    ctx.fillStyle = hovDot
      ? theme.accentSecondary
      : lerpHexColor(theme.dotInactive, theme.accentPrimary, ind.activeWeight);
    if (ind.mode === 'dashes') {
      traceRoundedRect(ctx, ind.x, ind.y, ind.width, ind.height, ind.radius);
    } else {
      traceCapsule(ctx, ind.x, ind.y, ind.width, ind.height);
    }
    ctx.fill();
  });

  if (isMouseModeActive(s) && s.totalPages > 1) {
    const axL = pSx >= 13 ? pSx - 12 : 2;
    const axR = pEx + 13 <= NL_SCREEN_W ? pEx + 7 : NL_SCREEN_W - 7;
    ctx.font = 'bold 8px "JetBrains Mono", monospace';
    ctx.textBaseline = 'middle';
    ctx.fillStyle = mAct && s.sui.hoverKind === 'PAGE_PREV' ? theme.accentPrimary : theme.textSecondary;
    ctx.fillText('<', axL, m.paginationY + 4);
    ctx.fillStyle = mAct && s.sui.hoverKind === 'PAGE_NEXT' ? theme.accentPrimary : theme.textSecondary;
    ctx.fillText('>', axR, m.paginationY + 4);
  }

  /* 5. Bottom Centered Shortcuts Bar (smoothly hidden when showShortcuts is OFF) */
  if (scT > 0.005) {
    const barY = Math.max(NL_BOTTOM_BAR_Y, Math.round(NL_SCREEN_H - NL_BOTTOM_BAR_H * scT));
    if (barY < NL_SCREEN_H) {
      ctx.fillStyle = theme.borderSubtle;
      ctx.fillRect(0, barY - 1, NL_SCREEN_W, 1);
      ctx.fillStyle = theme.bgBottomBar;
      ctx.fillRect(0, barY, NL_SCREEN_W, NL_SCREEN_H - barY);

      const shortcuts = [
        ['EXE/5', ' Jouer'],
        ['TAB', ' Cat.'],
        ['VAR', ' Fav.'],
        ['ESC', ' Quitter'],
      ];
      ctx.font = 'bold 7.5px "JetBrains Mono", monospace';
      ctx.textBaseline = 'middle';
      const widths = shortcuts.map(([k, l]) => ctx.measureText(k + l).width);
      const gap = 14;
      const totalW = widths.reduce((a, b) => a + b, 0) + gap * (shortcuts.length - 1);
      let sx = Math.round((NL_SCREEN_W - totalW) / 2);
      const sy = barY + 10;

      shortcuts.forEach(([k, l], idx) => {
        ctx.fillStyle = theme.accentSecondary;
        ctx.fillText(k, sx, sy);
        const kw = ctx.measureText(k).width;
        ctx.fillStyle = theme.textSecondary;
        ctx.fillText(l, sx + kw, sy);
        sx += widths[idx] + gap;
      });
    }
  }

  /* Quick Profile Selector Overlay (Library only) */
  if (anim.qpOpenAnim > 0.005 || s.sui.qpOpen) {
    drawQuickProfileOverlay(ctx, s, anim, theme, imgCache);
  }

  /* Organic asymmetric liquid-drop open/close from gear icon (302, 12) */
  if (isSettingsOpenOrClosing) {
    const activeSettingsScreen =
      s.sui.screen !== 'LIBRARY' ? s.sui.screen : anim.prevUiScreen;
    compositeSettingsLiquidDrop(ctx, s, anim, theme, activeSettingsScreen);
    if (anim.profPlusProgress < 1) {
      drawProfilePlusTransition(ctx, anim, theme);
    }
  }

  /* 4-Stage Profile Switch Transition over the newly loaded Library */
  if (anim.profSwitchProgress < 1) {
    drawProfileSwitchTransition(ctx, s, anim, theme, imgCache);
  }

  /* Optional Touchpad Graphical Cursor + Luminous Trail */
  drawTouchpadGraphicalCursor(ctx, s, anim, theme);

  /* 6. Toast Notification */
  renderToastBanner(ctx, anim, nowMs, theme);
}

function drawStartupSplashView(
  ctx: CanvasRenderingContext2D,
  s: LauncherState,
  elapsedMs: number,
  showLoader: boolean
) {
  const splashTheme = getConsoleTheme(s.displayMode, s.accentColor);
  ctx.fillStyle = splashTheme.bgMain;
  ctx.fillRect(0, 0, NL_SCREEN_W, NL_SCREEN_H);

  let size: number;
  if (elapsedMs < 240) {
    const t = Math.max(0, Math.min(1, elapsedMs / 240));
    const ease = 1 - Math.pow(1 - t, 3);
    size = Math.round(24 + 12 * ease);
  } else {
    const phase = (elapsedMs - 240) % 960;
    const pulse = 0.5 - 0.5 * Math.cos((phase / 480) * Math.PI);
    size = Math.round(36 + 8 * pulse);
  }

  const logoCx = NL_SCREEN_W / 2;
  const logoCy = 96;
  drawLauncherEmblem(
    ctx,
    Math.round(logoCx - size / 2),
    Math.round(logoCy - size / 2),
    size,
    splashTheme
  );

  ctx.fillStyle = splashTheme.textPrimary;
  ctx.font = 'bold 9.5px "JetBrains Mono", monospace';
  ctx.textBaseline = 'top';
  const title = 'N-LAUNCHER';
  ctx.fillText(title, Math.round((NL_SCREEN_W - title.length * 5.8) / 2), 127);

  if (showLoader) {
    const dotOffsets: [number, number][] = [
      [0, -10],
      [7, -7],
      [10, 0],
      [7, 7],
      [0, 10],
      [-7, 7],
      [-10, 0],
      [-7, -7],
    ];
    const spinCx = NL_SCREEN_W / 2;
    const spinCy = 158;
    const dotQuiet = lerpHexColor(
      splashTheme.bgMain,
      splashTheme.dotInactive,
      splashTheme.isLight ? 0.68 : 0.56
    );
    const stepMs = 85;
    const head = Math.floor(elapsedMs / stepMs) % 8;
    const sub = (elapsedMs % stepMs) / stepMs;

    for (let i = 0; i < 8; i++) {
      const dist = (head + 8 - i) & 7;
      let col = dotQuiet;
      if (dist === 0) {
        col = lerpHexColor(splashTheme.accentSecondary, splashTheme.accentPrimary, sub);
      } else if (dist === 1) {
        col = lerpHexColor(dotQuiet, splashTheme.accentPrimary, 1 - sub * 0.5);
      } else if (dist === 2) {
        col = lerpHexColor(dotQuiet, splashTheme.accentSecondary, (1 - sub) * 0.5);
      } else if (dist === 7) {
        col = lerpHexColor(dotQuiet, splashTheme.accentPrimary, sub * 0.5);
      }
      ctx.fillStyle = col;
      ctx.beginPath();
      ctx.arc(spinCx + dotOffsets[i][0], spinCy + dotOffsets[i][1], 2, 0, Math.PI * 2);
      ctx.fill();
    }
  }
}

NspireScreenCanvas.displayName = 'NspireScreenCanvas';
