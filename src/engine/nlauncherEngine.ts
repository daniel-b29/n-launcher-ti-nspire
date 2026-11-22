/**
 * N-Launcher v3.1 Core Logic & 320x240 RGB565 Simulator Engine
 * Exact TypeScript mirror of native modular C architecture
 * (scanner.c, covers.c, storage.c, ui_state.c, animation.c, renderer.c):
 * - Zehn / PRG / bFLT binary header inspection vs TI-Nspire ZIP documents
 * - Strict 3 categories ONLY: TOUS, FAV, REC
 * - Custom square-centric icon cards with area-averaged downsampling & AA borders
 * - Optimized grids (1x1, 2x2, 3x2, 3x3, 4x4) + smart adaptive pagination (<= 19 dots, > 19 dashes)
 */

export const NL_SCREEN_W = 320;
export const NL_SCREEN_H = 240;
export const NL_ICON_W = 94;
export const NL_ICON_H = 68;
export const NL_ICON_MAX_DIM = 84;
export const NL_GRID_CARD_W = 84;
export const NL_GRID_CARD_H = 84;
export const NL_COVER_RADIUS = 11;

export const NL_TOP_BAR_H = 25;
export const NL_VIEW_TOP = 26;
export const NL_VIEW_BOTTOM = 205;
export const NL_GRID_COLS = 3;
export const NL_GRID_ROWS = 2;
export const NL_GAMES_PER_PAGE = NL_GRID_COLS * NL_GRID_ROWS; // 6 games/page
export const NL_GRID_MARGIN_X = 18;
export const NL_GRID_GAP_X = 16;
export const NL_GRID_TOP_Y = 29;
export const NL_GRID_GAP_Y = 6;
export const NL_ROW_STRIDE_Y = NL_GRID_CARD_H + NL_GRID_GAP_Y; // 90 px
export const NL_PAGE_STRIDE_Y = NL_ROW_STRIDE_Y * NL_GRID_ROWS; // 180 px
export const NL_PAGINATION_Y = 209;
export const NL_PAGINATION_MAX_DOTS = 19;
export const NL_BOTTOM_BAR_Y = 221;
export const NL_BOTTOM_BAR_H = 19;
export const NL_VIEW_BOTTOM_NO_SC = 222;
export const NL_PAGINATION_Y_NO_SC = 226;

export const NL_GEAR_BTN_X = 295;
export const NL_GEAR_BTN_Y = 5;
export const NL_GEAR_BTN_D = 15;
export const NL_BAT_IND_X = 262;
export const NL_BAT_IND_Y = 7;
export const NL_BAT_IND_W = 24;
export const NL_BAT_IND_H = 11;
export const NL_BATTERY_REFRESH_INTERVAL_SEC = 3600;
export const NL_DISPLAY_OPT_COUNT = 5;
export const NL_BOOT_STABILIZE_FRAMES = 30;
export const NL_LAUNCHER_VERSION_STR = '3.1';
export const NL_BUILD_STAMP_STR = 'Build 2026-11-18 #R23';
export const NL_DIAG_MAX_ENTRIES = 24;
export const NL_BOOT_STAGE_COUNT = 12;

export type BootRecoveryReason =
  | 'none'
  | 'crash_detected'
  | 'manual_key'
  | 'missing_target'
  | 'exec_failed'
  | 'booting';

export type BootSimulationOutcome =
  | 'OK_CLEAN'
  | 'SKIP_NOT_STARTUP'
  | 'SKIP_DISABLED'
  | 'ABORT_RECOVERY_KEY'
  | 'ABORT_CRASH_GUARD'
  | 'ABORT_MISSING_TARGET'
  | 'ABORT_EXEC_FAILED'
  | 'CRASHED_MID_INIT';

export interface BootGuardState {
  pending: number;
  recovery: number;
  disabled: number;
  reason: BootRecoveryReason;
}

export function createCleanBootGuardState(): BootGuardState {
  return {
    pending: 0,
    recovery: 0,
    disabled: 0,
    reason: 'none',
  };
}

export type BatteryLevel = 'UNAVAILABLE' | 'LOW' | 'MEDIUM' | 'HIGH';

export const BATTERY_LEVEL_ORDER: BatteryLevel[] = ['HIGH', 'MEDIUM', 'LOW', 'UNAVAILABLE'];

export const BATTERY_LEVEL_LABELS: Record<BatteryLevel, string> = {
  HIGH: 'Eleve',
  MEDIUM: 'Moyen',
  LOW: 'Faible',
  UNAVAILABLE: 'Indisponible',
};

export const NL_SCALE_NORMAL = 1.0;
export const NL_SCALE_SELECTED = 280 / 256; // ~1.09375

export const NL_MAGIC_PRG = 0x00475250; // "PRG\0"
export const NL_MAGIC_ZEHN = 0x6e68655a; // "Zehn"
export const NL_MAGIC_BFLT = 0x544c4662; // "bFLT"
export const NL_MAGIC_PKZIP = 0x04034b50; // "PK\x03\x04"

export type BinFormat = 'ZEHN' | 'PRG' | 'BFLT' | 'TI-DOC' | 'SYSTEM' | 'INVALID';

export type CategoryId = 'ALL' | 'FAVORITES' | 'RECENT';

export type IconSource = 'NONE' | 'CAPTURED' | 'BMP' | 'ICO';

export type GameSceneKind =
  | 'DOOM'
  | 'QUAKE'
  | 'GBC'
  | 'GBA'
  | 'NES'
  | 'CRAFTI'
  | '2048'
  | 'PACMAN'
  | 'SHMUP'
  | 'RACING';

export type DisplayMode = 'dark' | 'light' | 'gray';

export const DISPLAY_MODE_ORDER: DisplayMode[] = ['dark', 'light', 'gray'];

export type AccentColor = 'blue' | 'red' | 'green' | 'yellow';

export const ACCENT_COLOR_ORDER: AccentColor[] = ['blue', 'red', 'green', 'yellow'];

export type LayoutMode = '1x1' | '2x2' | '3x2' | '3x3' | '4x4';

export const LAYOUT_ORDER: LayoutMode[] = ['1x1', '2x2', '3x2', '3x3', '4x4'];

export const LAYOUT_LABELS: Record<LayoutMode, string> = {
  '1x1': '1 x 1',
  '2x2': '2 x 2',
  '3x2': '3 x 2',
  '3x3': '3 x 3',
  '4x4': '4 x 4',
};

export type ScrollDir = 'vertical' | 'horizontal';

export const SCROLL_DIR_ORDER: readonly ScrollDir[] = ['vertical', 'horizontal'];

export const SCROLL_DIR_LABELS: Record<ScrollDir, string> = {
  vertical: 'Vertical',
  horizontal: 'Horizontal',
};

export type LaunchAnimMode = 'current' | 'theme_drop' | 'spinner' | 'loading_bar';

export const LAUNCH_ANIM_ORDER: readonly LaunchAnimMode[] = [
  'current',
  'theme_drop',
  'spinner',
  'loading_bar',
];

export const NL_LAUNCH_ANIM_COUNT = 4;

export const LAUNCH_ANIM_LABELS: Record<LaunchAnimMode, string> = {
  current: 'Actuelle (Capture)',
  theme_drop: 'Theme Drop',
  spinner: 'Spinner',
  loading_bar: 'Loading Bar',
};

export const LAUNCH_ANIM_SHORT_LABELS: Record<LaunchAnimMode, string> = {
  current: 'CAPTURE',
  theme_drop: 'LIQUIDE',
  spinner: 'SPINNER',
  loading_bar: 'BARRE',
};

export const LAUNCH_ANIM_SUBTITLES: Record<LaunchAnimMode, string> = {
  current: 'Zoom + halos + capture',
  theme_drop: '4 propagations liquides',
  spinner: 'Icone + spinner central',
  loading_bar: 'Centrage + barre reelle',
};

export function clampLaunchAnimMode(v: unknown): LaunchAnimMode {
  return v === 'theme_drop' || v === 'spinner' || v === 'loading_bar' ? v : 'current';
}

export type AppOpenAnimMode = 'current' | 'liquid' | 'zoom' | 'instant';

export const APP_OPEN_ANIM_ORDER: readonly AppOpenAnimMode[] = [
  'current',
  'liquid',
  'zoom',
  'instant',
];

export const NL_OPEN_ANIM_COUNT = 4;

export const APP_OPEN_ANIM_LABELS: Record<AppOpenAnimMode, string> = {
  current: 'Actuelle (Logo)',
  liquid: 'Liquide Organique',
  zoom: 'Zoom Fluide',
  instant: 'Instantane',
};

export const APP_OPEN_ANIM_SHORT_LABELS: Record<AppOpenAnimMode, string> = {
  current: 'LOGO',
  liquid: 'LIQUIDE',
  zoom: 'ZOOM',
  instant: 'DIRECT',
};

export const APP_OPEN_ANIM_SUBTITLES: Record<AppOpenAnimMode, string> = {
  current: 'Logo N-Launcher + fondu fluide',
  liquid: '4 propagations liquides organiques',
  zoom: 'Ouverture zoom fluide de la grille',
  instant: 'Acces immediat sans transition',
};

export function clampAppOpenAnimMode(v: unknown): AppOpenAnimMode {
  return v === 'liquid' || v === 'zoom' || v === 'instant' ? v : 'current';
}

export type AppCloseAnimMode = 'liquid' | 'fade' | 'zoom' | 'instant';

export const APP_CLOSE_ANIM_ORDER: readonly AppCloseAnimMode[] = [
  'liquid',
  'fade',
  'zoom',
  'instant',
];

export const NL_CLOSE_ANIM_COUNT = 4;

export const APP_CLOSE_ANIM_LABELS: Record<AppCloseAnimMode, string> = {
  liquid: 'Liquide Organique',
  fade: 'Fondu Fluide',
  zoom: 'Zoom Arriere',
  instant: 'Instantane',
};

export const APP_CLOSE_ANIM_SHORT_LABELS: Record<AppCloseAnimMode, string> = {
  liquid: 'LIQUIDE',
  fade: 'FONDU',
  zoom: 'ZOOM',
  instant: 'DIRECT',
};

export const APP_CLOSE_ANIM_SUBTITLES: Record<AppCloseAnimMode, string> = {
  liquid: 'Fermeture liquide en 4 ondes',
  fade: 'Extinction douce vers le theme',
  zoom: 'Retrait fluide vers le centre',
  instant: 'Sortie immediate vers TI-OS',
};

export function clampAppCloseAnimMode(v: unknown): AppCloseAnimMode {
  return v === 'fade' || v === 'zoom' || v === 'instant' ? v : 'liquid';
}

export type ReturnAnimMode = 'liquid' | 'zoom' | 'fade' | 'instant';

export const RETURN_ANIM_ORDER: readonly ReturnAnimMode[] = [
  'liquid',
  'zoom',
  'fade',
  'instant',
];

export const NL_RETURN_ANIM_COUNT = 4;

export const RETURN_ANIM_LABELS: Record<ReturnAnimMode, string> = {
  liquid: 'Liquide Inverse',
  zoom: 'Zoom Inverse',
  fade: 'Fondu Rapide',
  instant: 'Instantane',
};

export const RETURN_ANIM_SHORT_LABELS: Record<ReturnAnimMode, string> = {
  liquid: 'LIQUIDE',
  zoom: 'ZOOM',
  fade: 'FONDU',
  instant: 'DIRECT',
};

export const RETURN_ANIM_SUBTITLES: Record<ReturnAnimMode, string> = {
  liquid: '4 ondes liquides non bloquantes',
  zoom: 'Dezoom rapide sur la bibliotheque',
  fade: 'Voile doux immediatement interactif',
  instant: 'Retour brut sans animation',
};

export function clampReturnAnimMode(v: unknown): ReturnAnimMode {
  return v === 'zoom' || v === 'fade' || v === 'instant' ? v : 'liquid';
}

export const NL_ANIM_CAT_COUNT = 4;

export const ANIM_CATEGORY_LABELS: readonly string[] = [
  "Ouverture de l'app",
  "Fermeture de l'app",
  'Lancement des jeux',
  'Retour des jeux',
];

/**
 * #R23 Fast Return optimization (exact 1:1 mirror of native nl_return_needs_filter_rebuild):
 * Skip O(N log N) filter/sort rebuild on game return unless the active category or sort order
 * depends on play stats (REC category or last_used / most_launched / play_time sort).
 */
export function returnNeedsFilterRebuild(state: LauncherState): boolean {
  if (state.activeCategory === 'RECENT') return true;
  const sort = state.settings.sortOrder;
  return sort === 'last_used' || sort === 'most_launched' || sort === 'play_time';
}

export type SortOrder =
  | 'alpha_asc'
  | 'alpha_desc'
  | 'recent_add'
  | 'oldest_add'
  | 'last_used'
  | 'most_launched'
  | 'play_time';

export const SORT_ORDER_LIST: SortOrder[] = [
  'alpha_asc',
  'alpha_desc',
  'recent_add',
  'oldest_add',
  'last_used',
  'most_launched',
  'play_time',
];

export const SORT_ORDER_LABELS: Record<SortOrder, string> = {
  alpha_asc: 'Ordre alphabetique (A -> Z)',
  alpha_desc: 'Ordre alphabetique (Z -> A)',
  recent_add: 'Ajoutes recemment',
  oldest_add: 'Ajoutes le plus tot',
  last_used: 'Joues recemment',
  most_launched: 'Les plus lances',
  play_time: 'Temps de jeu cumule',
};

export type UiScreen =
  | 'LIBRARY'
  | 'SETTINGS_MAIN'
  | 'SETTINGS_AUTOSTART'
  | 'SETTINGS_DISPLAY'
  | 'SETTINGS_ANIMATIONS'
  | 'SETTINGS_TOUCHPAD'
  | 'SETTINGS_SHORTCUTS'
  | 'SETTINGS_KEY_HELP'
  | 'SETTINGS_SORT'
  | 'SETTINGS_STATS'
  | 'SETTINGS_CAPTURE_BATCH'
  | 'SETTINGS_ICONS'
  | 'SETTINGS_REFRESH_LIB'
  | 'SETTINGS_RESCAN'
  | 'SETTINGS_HOTKEY'
  | 'SETTINGS_PROFILES'
  | 'SETTINGS_DIAG'
  | 'SETTINGS_RESET'
  | 'SETTINGS_INFO'
  | 'SETTINGS_CREDITS';

export type SettingsIconId =
  | 'POWER'
  | 'SCREEN'
  | 'TOUCHPAD'
  | 'LAUNCH_KEY'
  | 'KEYBOARD'
  | 'SORT'
  | 'STATS'
  | 'SEARCH'
  | 'RESET'
  | 'INFO'
  | 'CREDITS'
  | 'GRID'
  | 'TAG'
  | 'BOTTOMBAR'
  | 'BATTERY'
  | 'REFRESH'
  | 'CAPTURE'
  | 'ICONS'
  | 'HOTKEY'
  | 'PROFILES'
  | 'DIAG'
  | 'ANIM';

export const NL_AVATAR_COUNT = 43;
export const NL_AVATAR_CAT_COUNT = 4;
export const NL_MAX_PROFILES = 43;
export const NL_PROFILE_NAME_MAX = 15;

export interface AvatarCategoryDef {
  id: number;
  label: string;
  start: number;
  count: number;
}

export const AVATAR_CATEGORIES: readonly AvatarCategoryDef[] = [
  { id: 0, label: 'Jeux video', start: 0, count: 16 },
  { id: 1, label: 'Fortnite', start: 16, count: 9 },
  { id: 2, label: 'Heroiques', start: 25, count: 9 },
  { id: 3, label: 'Rabbins', start: 34, count: 9 },
];

export function getAvatarImageUrl(avatarId: number): string {
  const clamped = Math.max(0, Math.min(NL_AVATAR_COUNT - 1, Math.floor(avatarId)));
  return `/avatars/avatar_${String(clamped).padStart(2, '0')}.png`;
}

export function getAvatarCategoryLabel(catIdx: number): string {
  return AVATAR_CATEGORIES[catIdx]?.label ?? AVATAR_CATEGORIES[0].label;
}

export function getAvatarCategoryRange(catIdx: number): { start: number; count: number } {
  const cat = AVATAR_CATEGORIES[catIdx] ?? AVATAR_CATEGORIES[0];
  return { start: cat.start, count: cat.count };
}

export function getAvatarCategoryOf(avatarId: number): number {
  if (avatarId < 16) return 0;
  if (avatarId < 25) return 1;
  if (avatarId < 34) return 2;
  return 3;
}

export type ProfileUiMode =
  | 'LIST'
  | 'DETAIL'
  | 'EDIT_NAME'
  | 'EDIT_AVATAR'
  | 'CREATE_NAME'
  | 'CREATE_AVATAR'
  | 'CONFIRM_DELETE';

export interface ProfileGameData {
  isFavorite: boolean;
  lastPlayedRank: number;
  launchCount: number;
  playTimeSec: number;
  lastPlayedTs: number;
  shortcutKey: string;
}

export interface UserProfile {
  id: number;
  name: string;
  avatarId: number;
  isPrimary: boolean;
  isLocked?: boolean;
  layout: LayoutMode;
  scrollDir?: ScrollDir;
  appOpenAnim?: AppOpenAnimMode;
  appCloseAnim?: AppCloseAnimMode;
  launchAnimMode?: LaunchAnimMode;
  returnAnimMode?: ReturnAnimMode;
  showNames: boolean;
  showShortcuts: boolean;
  sortOrder: SortOrder;
  activeCategory: CategoryId;
  selectedPos: number;
  playCounter: number;
  gameData: Record<string, ProfileGameData>;
}

export interface ProfileSystemState {
  items: UserProfile[];
  activeIndex: number;
  nextProfileId: number;
}

export interface SettingsMenuItemDef {
  id: Exclude<UiScreen, 'LIBRARY' | 'SETTINGS_MAIN'>;
  label: string;
  subtitle: string;
  icon: SettingsIconId;
}

export const SETTINGS_MENU_ITEMS: SettingsMenuItemDef[] = [
  {
    id: 'SETTINGS_DISPLAY',
    label: 'Affichage',
    subtitle: "Grille, titres et barre d'aide",
    icon: 'SCREEN',
  },
  {
    id: 'SETTINGS_ANIMATIONS',
    label: 'Animations',
    subtitle: 'Animation de lancement des jeux',
    icon: 'ANIM',
  },
  {
    id: 'SETTINGS_TOUCHPAD',
    label: 'Navigation tactile',
    subtitle: 'Deplacer le curseur au doigt',
    icon: 'TOUCHPAD',
  },
  {
    id: 'SETTINGS_SHORTCUTS',
    label: 'Touches de lancement',
    subtitle: 'Touche directe vers un jeu',
    icon: 'LAUNCH_KEY',
  },
  {
    id: 'SETTINGS_KEY_HELP',
    label: 'Commandes du launcher',
    subtitle: 'Touches et actions systeme',
    icon: 'KEYBOARD',
  },
  {
    id: 'SETTINGS_SORT',
    label: 'Tri de la bibliotheque',
    subtitle: "Ordre d'affichage des jeux",
    icon: 'SORT',
  },
  {
    id: 'SETTINGS_STATS',
    label: 'Statistiques de jeu',
    subtitle: 'Temps de jeu et sessions',
    icon: 'STATS',
  },
  {
    id: 'SETTINGS_CAPTURE_BATCH',
    label: "Generer captures d'ecran",
    subtitle: 'Capturer tous les jeux',
    icon: 'CAPTURE',
  },
  {
    id: 'SETTINGS_ICONS',
    label: 'Gestion des icones',
    subtitle: 'Voir ou supprimer une icone',
    icon: 'ICONS',
  },
  {
    id: 'SETTINGS_REFRESH_LIB',
    label: 'Rescanner les jeux',
    subtitle: 'Verifier dossiers et actualiser',
    icon: 'REFRESH',
  },
  {
    id: 'SETTINGS_HOTKEY',
    label: 'Raccourci global TI-OS',
    subtitle: 'Ouvrir depuis menus TI-OS (manuel)',
    icon: 'HOTKEY',
  },
  {
    id: 'SETTINGS_PROFILES',
    label: 'Profils utilisateurs',
    subtitle: 'Comptes, avatars et donnees',
    icon: 'PROFILES',
  },
  {
    id: 'SETTINGS_DIAG',
    label: 'Diagnostics & Journaux',
    subtitle: 'Erreurs, boot et journaux',
    icon: 'DIAG',
  },
  {
    id: 'SETTINGS_RESET',
    label: 'Reinitialiser les reglages',
    subtitle: 'Valeurs par defaut sans perte',
    icon: 'RESET',
  },
  {
    id: 'SETTINGS_INFO',
    label: 'Informations systeme',
    subtitle: 'Version, TI-OS et Ndless',
    icon: 'INFO',
  },
  {
    id: 'SETTINGS_CREDITS',
    label: 'A propos et credits',
    subtitle: 'Createur et projet N Launcher',
    icon: 'CREDITS',
  },
];

export const ASSIGNABLE_SHORTCUT_KEYS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ13';

export const NL_TP_STEP_THRESHOLD = 215;
export const NL_TP_SENS_COUNT = 5;
export const NL_TP_SENS_DEFAULT = 2;
export const NL_TP_THRESHOLDS: readonly number[] = [300, 255, 215, 175, 138];
export const TP_SENS_LABELS: readonly string[] = [
  '1/5 Lente',
  '2/5 Douce',
  '3/5 Normale',
  '4/5 Sensible',
  '5/5 Rapide',
];

export const NL_CURSOR_SPEED_COUNT = 5;
export const NL_CURSOR_SPEED_DEFAULT = 2;
export const NL_CURSOR_SPEED_SCALES: readonly number[] = [8, 12, 17, 23, 30];
export const CURSOR_SPEED_LABELS: readonly string[] = [
  '1/5 Tres lente',
  '2/5 Lente',
  '3/5 Normale',
  '4/5 Rapide',
  '5/5 Tres rap.',
];

export type HotkeyCombo = 'ctrl_menu' | 'shift_menu' | 'ctrl_shift_menu';

export const HOTKEY_COMBO_ORDER: readonly HotkeyCombo[] = [
  'ctrl_menu',
  'shift_menu',
  'ctrl_shift_menu',
];

export const HOTKEY_COMBO_LABELS: Record<HotkeyCombo, string> = {
  ctrl_menu: 'Ctrl + Menu',
  shift_menu: 'Shift + Menu',
  ctrl_shift_menu: 'Ctrl + Shift + Menu',
};

export const HOTKEY_COMBO_SHORT_TAGS: Record<HotkeyCombo, string> = {
  ctrl_menu: 'CTRL+MENU',
  shift_menu: 'SHFT+MENU',
  ctrl_shift_menu: 'C+S+MENU',
};

export type TouchpadMode = 'touchpad' | 'mouse';

export const TOUCHPAD_MODE_LABELS: Record<TouchpadMode, string> = {
  touchpad: 'Pave tactile',
  mouse: 'Souris',
};

export type CursorStyle = 'none' | 'arrow' | 'circle' | 'pointer';

export const CURSOR_STYLE_ORDER: readonly CursorStyle[] = ['arrow', 'circle', 'pointer'];

export const CURSOR_STYLE_LABELS: Record<CursorStyle, string> = {
  none: 'Aucun curseur',
  arrow: 'Fleche',
  circle: 'Cercle',
  pointer: 'Pointeur',
};

export type CursorSize = 'small' | 'medium' | 'large';

export const CURSOR_SIZE_ORDER: readonly CursorSize[] = ['small', 'medium', 'large'];

export const CURSOR_SIZE_LABELS: Record<CursorSize, string> = {
  small: 'Petite',
  medium: 'Moyenne',
  large: 'Grande',
};

export const NL_CURSOR_SIZE_DEFAULT: CursorSize = 'medium';

export function clampCursorSize(size: unknown): CursorSize {
  if (size === 'small' || size === 'medium' || size === 'large') {
    return size;
  }
  return NL_CURSOR_SIZE_DEFAULT;
}

export function cursorSizeToIndex(size: CursorSize): 0 | 1 | 2 {
  if (size === 'small') return 0;
  if (size === 'large') return 2;
  return 1;
}

export function indexToCursorSize(idx: number): CursorSize {
  if (idx === 0) return 'small';
  if (idx === 2) return 'large';
  return 'medium';
}

export function getCursorSizeMetrics(
  style: CursorStyle,
  size: CursorSize
): { width: number; height: number; hotX: number; hotY: number; scale: number } {
  const s = clampCursorSize(size);
  if (style === 'pointer') {
    if (s === 'small') return { width: 13, height: 16, hotX: 4, hotY: 1, scale: 16 / 28 };
    if (s === 'large') return { width: 22, height: 28, hotX: 7, hotY: 2, scale: 1.0 };
    return { width: 17, height: 22, hotX: 5, hotY: 1, scale: 22 / 28 };
  }
  if (style === 'circle') {
    if (s === 'small') return { width: 15, height: 15, hotX: 7, hotY: 7, scale: 15 / 28 };
    if (s === 'large') return { width: 28, height: 28, hotX: 14, hotY: 14, scale: 1.0 };
    return { width: 21, height: 21, hotX: 10, hotY: 10, scale: 21 / 28 };
  }
  if (s === 'small') return { width: 15, height: 15, hotX: 1, hotY: 1, scale: 15 / 28 };
  if (s === 'large') return { width: 28, height: 28, hotX: 2, hotY: 2, scale: 1.0 };
  return { width: 21, height: 21, hotX: 2, hotY: 2, scale: 21 / 28 };
}

export const NL_TP_TAB0_ROW_COUNT = 7;
export const NL_TP_TAB0_VISIBLE_ROWS = 5;
export const NL_TP_TAB0_ROW_HEIGHTS: readonly number[] = [26, 25, 37, 37, 26, 26, 28];

export function getTouchpadTab0RowRects(tpScroll: number): Array<{ row: number; y: number; h: number }> {
  const clampedScroll = Math.max(0, Math.min(NL_TP_TAB0_ROW_COUNT - NL_TP_TAB0_VISIBLE_ROWS, Math.trunc(tpScroll)));
  const rects: Array<{ row: number; y: number; h: number }> = [];
  let y = 47;
  for (let vis = 0; vis < NL_TP_TAB0_VISIBLE_ROWS; vis++) {
    const row = clampedScroll + vis;
    if (row >= NL_TP_TAB0_ROW_COUNT) break;
    const h = NL_TP_TAB0_ROW_HEIGHTS[row];
    rects.push({ row, y, h });
    y += h + 2;
  }
  return rects;
}

export type HoverTargetKind =
  | 'NONE'
  | 'PROFILE_BADGE'
  | 'CATEGORY'
  | 'FAV_BTN'
  | 'SETTINGS_BTN'
  | 'SEARCH_BAR'
  | 'SEARCH_CLOSE'
  | 'PAGE_PREV'
  | 'PAGE_NEXT'
  | 'PAGE_DOT'
  | 'GAME_CARD'
  | 'QP_PREV'
  | 'QP_NEXT'
  | 'QP_SLOT'
  | 'QP_OUTSIDE'
  | 'SET_ESC'
  | 'SET_POWER'
  | 'SET_TAB'
  | 'SET_ROW'
  | 'SET_SLIDER_NOTCH'
  | 'SET_STYLE_CARD'
  | 'SET_SIZE_CARD'
  | 'SC_KEY_CELL'
  | 'ICON_CARD'
  | 'PROF_ROW'
  | 'PROF_DETAIL_ROW'
  | 'PROF_AV_TAB'
  | 'PROF_AV_CELL'
  | 'DIAG_TAB'
  | 'DIAG_ROW'
  | 'DIAG_CLEAR_BTN'
  | 'DIAG_DETAIL_CLOSE'
  | 'MODAL_BTN';

export function clampCursorStyle(style: unknown): CursorStyle {
  if (style === 'none' || style === 'arrow' || style === 'circle' || style === 'pointer') {
    return style;
  }
  return 'arrow';
}

export function clampCursorStyleToCard(style: CursorStyle): 0 | 1 | 2 {
  if (style === 'circle') return 1;
  if (style === 'pointer') return 2;
  return 0;
}

export function cardIndexToCursorStyle(cardIdx: number): CursorStyle {
  if (cardIdx === 1) return 'circle';
  if (cardIdx === 2) return 'pointer';
  return 'arrow';
}

export function clampCursorSpeed(level: number): number {
  if (!Number.isFinite(level)) return NL_CURSOR_SPEED_DEFAULT;
  const n = Math.round(level);
  if (n < 0) return 0;
  if (n >= NL_CURSOR_SPEED_COUNT) return NL_CURSOR_SPEED_COUNT - 1;
  return n;
}

export function clampTouchpadSensitivity(level: number): number {
  if (!Number.isFinite(level)) return NL_TP_SENS_DEFAULT;
  const n = Math.round(level);
  if (n < 0) return 0;
  if (n >= NL_TP_SENS_COUNT) return NL_TP_SENS_COUNT - 1;
  return n;
}

export function getTouchpadThresholdForLevel(level: number): number {
  return NL_TP_THRESHOLDS[clampTouchpadSensitivity(level)];
}

export interface TouchpadTracker {
  touching: boolean;
  lastX: number;
  lastY: number;
  accX: number;
  accY: number;
  cooldownTicks: number;
  activeFrames: number;
}

export function createTouchpadTracker(): TouchpadTracker {
  return {
    touching: false,
    lastX: 0,
    lastY: 0,
    accX: 0,
    accY: 0,
    cooldownTicks: 0,
    activeFrames: 0,
  };
}

export function resetTouchpadTracker(tracker: TouchpadTracker): void {
  tracker.touching = false;
  tracker.lastX = 0;
  tracker.lastY = 0;
  tracker.accX = 0;
  tracker.accY = 0;
  tracker.cooldownTicks = 0;
  tracker.activeFrames = 0;
}

export function processTouchpadSample(
  tracker: TouchpadTracker,
  enabled: boolean,
  contact: boolean,
  rawX: number,
  rawY: number,
  padWidth = 2328,
  padHeight = 1691,
  invertY = true,
  sensitivity = NL_TP_SENS_DEFAULT
): NavKey | null {
  if (!enabled || !contact) {
    resetTouchpadTracker(tracker);
    return null;
  }

  const w = padWidth > 200 ? padWidth : 2328;
  const h = padHeight > 200 ? padHeight : 1691;

  if (!tracker.touching) {
    tracker.touching = true;
    tracker.lastX = rawX;
    tracker.lastY = rawY;
    tracker.accX = 0;
    tracker.accY = 0;
    tracker.cooldownTicks = 0;
    tracker.activeFrames = 1;
    return null;
  }

  tracker.activeFrames++;
  if (tracker.cooldownTicks > 0) {
    tracker.cooldownTicks--;
  }

  const dxRaw = rawX - tracker.lastX;
  const dyScreen = invertY ? tracker.lastY - rawY : rawY - tracker.lastY;
  tracker.lastX = rawX;
  tracker.lastY = rawY;

  let dxNorm = Math.trunc((dxRaw * 1024) / w);
  let dyNorm = Math.trunc((dyScreen * 1024) / h);

  if (Math.abs(dxNorm) > 460 || Math.abs(dyNorm) > 460) {
    tracker.accX = 0;
    tracker.accY = 0;
    return null;
  }

  if (Math.abs(dxNorm) < 4) dxNorm = 0;
  if (Math.abs(dyNorm) < 4) dyNorm = 0;

  if ((dxNorm > 0 && tracker.accX < 0) || (dxNorm < 0 && tracker.accX > 0)) {
    tracker.accX = 0;
  }
  if ((dyNorm > 0 && tracker.accY < 0) || (dyNorm < 0 && tracker.accY > 0)) {
    tracker.accY = 0;
  }

  tracker.accX += dxNorm;
  tracker.accY += dyNorm;

  if (tracker.cooldownTicks > 0) {
    return null;
  }

  const absX = Math.abs(tracker.accX);
  const absY = Math.abs(tracker.accY);
  const thresh = getTouchpadThresholdForLevel(sensitivity);

  if (absX >= thresh || absY >= thresh) {
    let out: NavKey;
    if (absX >= absY) {
      if (tracker.accX > 0) {
        out = 'RIGHT';
        tracker.accX -= thresh;
        if (tracker.accX > 90) tracker.accX = 90;
      } else {
        out = 'LEFT';
        tracker.accX += thresh;
        if (tracker.accX < -90) tracker.accX = -90;
      }
      tracker.accY = Math.trunc(tracker.accY / 2);
    } else {
      if (tracker.accY > 0) {
        out = 'DOWN';
        tracker.accY -= thresh;
        if (tracker.accY > 90) tracker.accY = 90;
      } else {
        out = 'UP';
        tracker.accY += thresh;
        if (tracker.accY < -90) tracker.accY = -90;
      }
      tracker.accX = Math.trunc(tracker.accX / 2);
    }
    tracker.cooldownTicks = 4;
    return out;
  }

  return null;
}

export const NL_TP_CLICK_SLIDE_CANCEL = 140;

export interface TouchpadClickTracker {
  inContact: boolean;
  movedSinceDown: boolean;
  waitRelease: boolean;
  releaseStreak: number;
  cooldownTicks: number;
  downX: number;
  downY: number;
}

export function createTouchpadClickTracker(): TouchpadClickTracker {
  return {
    inContact: false,
    movedSinceDown: false,
    waitRelease: false,
    releaseStreak: 0,
    cooldownTicks: 0,
    downX: 0,
    downY: 0,
  };
}

export function resetTouchpadClickTracker(guard: TouchpadClickTracker): void {
  guard.inContact = false;
  guard.movedSinceDown = false;
  guard.waitRelease = false;
  guard.releaseStreak = 0;
  guard.cooldownTicks = 0;
  guard.downX = 0;
  guard.downY = 0;
}

export function processTouchpadClick(
  guard: TouchpadClickTracker,
  contact: boolean,
  pressed: boolean,
  centerClick: boolean,
  rawX: number,
  rawY: number,
  padWidth = 2328,
  padHeight = 1691
): boolean {
  if (guard.cooldownTicks > 0) {
    guard.cooldownTicks--;
  }

  if (guard.waitRelease) {
    if (!pressed && !contact) {
      guard.releaseStreak++;
      if (guard.releaseStreak >= 2) {
        guard.waitRelease = false;
        guard.releaseStreak = 0;
      }
    } else {
      guard.releaseStreak = 0;
    }
    return false;
  }

  if (contact || pressed) {
    if (!guard.inContact) {
      guard.inContact = true;
      guard.downX = rawX;
      guard.downY = rawY;
      guard.movedSinceDown = false;
    } else if (contact && !pressed) {
      const w = padWidth >= 200 ? padWidth : 2328;
      const h = padHeight >= 200 ? padHeight : 1691;
      const dxNorm = Math.trunc(((rawX - guard.downX) * 1024) / w);
      const dyNorm = Math.trunc(((rawY - guard.downY) * 1024) / h);
      if (Math.abs(dxNorm) > NL_TP_CLICK_SLIDE_CANCEL || Math.abs(dyNorm) > NL_TP_CLICK_SLIDE_CANCEL) {
        guard.movedSinceDown = true;
      }
    }
  } else {
    guard.inContact = false;
    guard.movedSinceDown = false;
  }

  if (pressed && centerClick && !guard.movedSinceDown && guard.cooldownTicks === 0) {
    guard.waitRelease = true;
    guard.releaseStreak = 0;
    guard.cooldownTicks = 6;
    guard.inContact = false;
    guard.movedSinceDown = false;
    return true;
  }

  return false;
}

export type NavKey =
  | 'LEFT'
  | 'RIGHT'
  | 'UP'
  | 'DOWN'
  | 'EXE'
  | 'ESC'
  | 'TAB'
  | 'FAV'
  | 'RESCAN'
  | 'PAGE_PREV'
  | 'PAGE_NEXT'
  | 'DOCK'
  | 'MENU'
  | 'SETTINGS'
  | 'BACKSPACE'
  | 'SPACE'
  | 'PROFILE_QUICK'
  | 'PROFILE_PREV'
  | 'PROFILE_NEXT';

export interface LayoutMetrics {
  cols: number;
  rows: number;
  perPage: number;
  cardW: number;
  cardH: number;
  radius: number;
  marginX: number;
  gapX: number;
  topY: number;
  gapY: number;
  labelH: number;
  rowStrideY: number;
  pageStrideY: number;
  pageStrideX: number;
  isHorizontal: boolean;
  viewBottom: number;
  paginationY: number;
}

export interface LauncherSettings {
  autostart: boolean;
  globalHotkey: boolean;
  hotkeyCombo: HotkeyCombo;
  layout: LayoutMode;
  scrollDir?: ScrollDir;
  appOpenAnim?: AppOpenAnimMode;
  appCloseAnim?: AppCloseAnimMode;
  launchAnimMode?: LaunchAnimMode;
  returnAnimMode?: ReturnAnimMode;
  showNames: boolean;
  showShortcuts: boolean;
  showBattery: boolean;
  profileLockEnabled?: boolean;
  touchpadMode: TouchpadMode;
  touchpadNav: boolean;
  touchpadSensitivity: number;
  cursorSpeed: number;
  touchpadCursorShow: boolean;
  touchpadCursorStyle: CursorStyle;
  cursorSize: CursorSize;
  tapToClick: boolean;
  sortOrder: SortOrder;
}

export type DiagCode =
  | 'E102'
  | 'E103'
  | 'E104'
  | 'E201'
  | 'E301'
  | 'E401'
  | 'E501'
  | 'E601'
  | 'E801'
  | 'E901';

export type HardwarePresetId = 'CX2T_620' | 'CXCAS_450';

export interface DiagEntry {
  code: DiagCode;
  stage: number;
  count: number;
  resolved: boolean;
  category: string;
  title: string;
  target: string;
  path: string;
  operation: string;
  reason: string;
  impact: string;
  recovery: string;
}

export interface DiagStore {
  entries: DiagEntry[];
  currentStage: number;
  stageReached: boolean[];
  warningSummaryShown: boolean;
  logBytes: number;
}

export interface SysEnvSim {
  hasNdless: boolean;
  ndlessPersistent: boolean;
  ndlessRev: number;
  hwIsCx2: boolean;
  osVersion: string;
  autostartActive: boolean;
  autostartDiag: string;
  hotkeyActive: boolean;
  hotkeyRamActive: boolean;
  hotkeyDiag: string;
  cacheValid: boolean;
  cacheCrc32: number;
  cacheLoadedAtBoot: boolean;
  oomRecoveries: number;
  priorIncompleteBoot: boolean;
  lastCrashStage: number;
  lastCrashMode: string;
  bootRecoveryTriggered: boolean;
  bootRecoveryReason: BootRecoveryReason;
  batteryLevel: BatteryLevel;
  tiosBatterySource: BatteryLevel;
  batteryLastRtc: number;
  batteryReadCount: number;
}

export interface RescanSummary {
  dirsScanned: number;
  filesExamined: number;
  gamesFound: number;
  gamesAdded: number;
  gamesKept: number;
  gamesRemoved: number;
  iconsLoaded: number;
  iconsFallback: number;
  memorizedFolders: number;
  emptyConfirmPending: boolean;
  emptyConfirmReplace: boolean;
  finished: boolean;
  rescanCount: number;
}

export function createDefaultRescanSummary(): RescanSummary {
  return {
    dirsScanned: 0,
    filesExamined: 0,
    gamesFound: 0,
    gamesAdded: 0,
    gamesKept: 0,
    gamesRemoved: 0,
    iconsLoaded: 0,
    iconsFallback: 0,
    memorizedFolders: 0,
    emptyConfirmPending: false,
    emptyConfirmReplace: false,
    finished: false,
    rescanCount: 0,
  };
}

export interface SettingsUiState {
  screen: UiScreen;
  menuCursor: number;
  menuScroll: number;
  displayCursor: number;
  displayScroll: number;
  animCursor: number;
  hotkeyCursor: 0 | 1;
  rescan: RescanSummary;
  tpCursor: number;
  tpScroll: number;
  tpTab: 0 | 1;
  tpCustomRow: 0 | 1;
  tpStyleCursor: number;
  tpSizeCursor: number;
  scGameCursor: number;
  scGameScroll: number;
  scPickingKey: boolean;
  scKeyCursor: number;
  scConflictPrompt: boolean;
  scConflictKey: string;
  scConflictOwnerIdx: number;
  scConflictConfirmYes: boolean;
  subCursor: number;
  subScroll: number;
  searchActive: boolean;
  searchQuery: string;
  resetConfirmYes: boolean;
  resetPerformed: boolean;
  capActive: boolean;
  capSemiAuto: boolean;
  capCancelled: boolean;
  capQueue: number[];
  capTotal: number;
  capPos: number;
  capDoneCount: number;
  capSkipCount: number;
  iconCursor: number;
  iconScrollRow: number;
  iconConfirmModal: boolean;
  iconConfirmYes: boolean;
  poweroffRequested: boolean;
  qpOpen: boolean;
  qpCursor: number;
  profMode: ProfileUiMode;
  profCursor: number;
  profScroll: number;
  profDetailIdx: number;
  profDetailCursor: number;
  profEditName: string;
  profAvatarCat: number;
  profAvatarCursor: number;
  profDeleteYes: boolean;
  profFromQuick: boolean;
  diagTab: 0 | 1;
  diagCursor: number;
  diagScroll: number;
  diagDetailOpen: boolean;
  diagConfirmModal: boolean;
  diagConfirmYes: boolean;
  diagClearReq: boolean;
  ctrlMouseActive: boolean;
  mouseX: number;
  mouseY: number;
  hoverValid: boolean;
  hoverKind: HoverTargetKind;
  hoverIndex: number;
  hoverSub: number;
}

export interface ShortcutDef {
  keyLabel: string;
  actionLabel: string;
}

export interface TnsInspectionResult {
  format: BinFormat;
  isExecutable: boolean;
  fileSize: number;
  magicHex: string;
  zehnOffset: number | null;
  zehnVersion: number | null;
  relocCount: number;
  allocSize: number;
  ndlessMin: number;
  usesLcdBlit: boolean;
  supportsHww: boolean;
  execName: string;
  execAuthor: string;
  execVersion: number;
  reason: string;
}

export interface VirtualFileEntry {
  path: string;
  sizeBytes: number;
  headerBytes: Uint8Array;
  addedOrder?: number;
}

export interface GameEntry {
  id: string;
  title: string;
  author: string;
  path: string;
  filename: string;
  sizeBytes: number;
  binFormat: 'ZEHN' | 'PRG' | 'BFLT';
  sceneKind: GameSceneKind;
  execVersion: number;
  relocCount: number;
  zehnOffset: number;
  usesLcdBlit: boolean;
  supportsHww: boolean;
  isFavorite: boolean;
  lastPlayedRank: number;
  addedOrder: number;
  launchCount: number;
  playTimeSec: number;
  lastPlayedTs: number;
  shortcutKey: string;
  accentHex: string;
  iconSource: IconSource;
  capturedIconDataUrl: string | null;
  hasScreenshot: boolean;
  screenshotDataUrl: string | null;
}

export interface ScannedFileDiagnostic {
  path: string;
  filename: string;
  sizeBytes: number;
  status: 'ACCEPTED' | 'REJECTED_TI_DOC' | 'REJECTED_SYSTEM' | 'REJECTED_DUPLICATE' | 'REJECTED_INVALID';
  inspection: TnsInspectionResult;
}

export interface LauncherState {
  games: GameEntry[];
  filteredIndices: number[];
  selectedPos: number;
  currentPage: number;
  totalPages: number;
  activeCategory: CategoryId;
  displayMode: DisplayMode;
  accentColor: AccentColor;
  settings: LauncherSettings;
  sys: SysEnvSim;
  sui: SettingsUiState;
  profiles: ProfileSystemState;
  launchHolding?: boolean;
  runningGameIndex: number | null;
  runningStartMs: number | null;
  lastExecStatus: number;
  playCounter: number;
  navSeq: number;
  mouseSeq?: number;
  toastMessage: string | null;
  toastSeq: number;
  profSwitchSeq?: number;
  profSwitchFromAvatarId?: number;
  profSwitchAvatarId?: number;
  profSwitchOriginX?: number;
  profSwitchOriginY?: number;
  profSwitchOriginD?: number;
  profPlusSeq?: number;
  profPlusOriginX?: number;
  profPlusOriginY?: number;
  profBirthSeq?: number;
  profBirthAvatarId?: number;
  profBirthSlot?: number;
  returnDropSeq?: number;
  returnDropGameIdx?: number;
  diag: DiagStore;
  diagnostics: ScannedFileDiagnostic[];
  stats: {
    scannedFiles: number;
    acceptedGames: number;
    rejectedDocs: number;
    rejectedSys: number;
    rejectedDups: number;
  };
}

export interface ConsoleThemePalette {
  bgMain: string;
  bgMainRgb: [number, number, number];
  bgTopBar: string;
  bgBottomBar: string;
  cardBg: string;
  cardBgSel: string;
  borderSubtle: string;
  dotInactive: string;
  textPrimary: string;
  textSecondary: string;
  accentPrimary: string;
  accentSecondary: string;
  textOnAccent: string;
  badgeFav: string;
  pillBg: string;
  isLight: boolean;
}

const THEME_MATRIX: Record<DisplayMode, Record<AccentColor, ConsoleThemePalette>> = {
  dark: {
    blue: {
      bgMain: '#0d1117',
      bgMainRgb: [13, 17, 23],
      bgTopBar: '#141923',
      bgBottomBar: '#10141c',
      cardBg: '#19202c',
      cardBgSel: '#222b3a',
      borderSubtle: '#2e3848',
      dotInactive: '#4e586c',
      textPrimary: '#f0f4fa',
      textSecondary: '#8896ac',
      accentPrimary: '#38bdf8',
      accentSecondary: '#7dd3fc',
      textOnAccent: '#0a101a',
      badgeFav: '#facc15',
      pillBg: '#222b3a',
      isLight: false,
    },
    red: {
      bgMain: '#0f1015',
      bgMainRgb: [15, 16, 21],
      bgTopBar: '#18161d',
      bgBottomBar: '#131218',
      cardBg: '#1e1c25',
      cardBgSel: '#292531',
      borderSubtle: '#383242',
      dotInactive: '#5c5466',
      textPrimary: '#f5f3f7',
      textSecondary: '#9c94a6',
      accentPrimary: '#ef4444',
      accentSecondary: '#f87171',
      textOnAccent: '#ffffff',
      badgeFav: '#facc15',
      pillBg: '#27232f',
      isLight: false,
    },
    green: {
      bgMain: '#0d1211',
      bgMainRgb: [13, 18, 17],
      bgTopBar: '#131b19',
      bgBottomBar: '#0f1614',
      cardBg: '#182320',
      cardBgSel: '#202f2a',
      borderSubtle: '#2d413a',
      dotInactive: '#4c645c',
      textPrimary: '#f0f7f4',
      textSecondary: '#8aa49b',
      accentPrimary: '#22c55e',
      accentSecondary: '#4ade80',
      textOnAccent: '#0a180f',
      badgeFav: '#facc15',
      pillBg: '#1f2d28',
      isLight: false,
    },
    yellow: {
      bgMain: '#101012',
      bgMainRgb: [16, 16, 18],
      bgTopBar: '#19181a',
      bgBottomBar: '#141315',
      cardBg: '#201f22',
      cardBgSel: '#2c2a2e',
      borderSubtle: '#3e3b40',
      dotInactive: '#625e66',
      textPrimary: '#f8f6f0',
      textSecondary: '#a49e90',
      accentPrimary: '#facc15',
      accentSecondary: '#fde047',
      textOnAccent: '#181406',
      badgeFav: '#facc15',
      pillBg: '#2a282c',
      isLight: false,
    },
  },
  light: {
    blue: {
      bgMain: '#f4f6fa',
      bgMainRgb: [244, 246, 250],
      bgTopBar: '#ffffff',
      bgBottomBar: '#edf1f7',
      cardBg: '#e3e8f0',
      cardBgSel: '#d6deea',
      borderSubtle: '#cbd5e1',
      dotInactive: '#94a3b8',
      textPrimary: '#0f172a',
      textSecondary: '#475569',
      accentPrimary: '#0284c7',
      accentSecondary: '#0369a1',
      textOnAccent: '#ffffff',
      badgeFav: '#eab308',
      pillBg: '#e2e8f0',
      isLight: true,
    },
    red: {
      bgMain: '#f9f5f6',
      bgMainRgb: [249, 245, 246],
      bgTopBar: '#ffffff',
      bgBottomBar: '#f3edef',
      cardBg: '#ece3e6',
      cardBgSel: '#e2d5d9',
      borderSubtle: '#d6c8cd',
      dotInactive: '#a2949a',
      textPrimary: '#181014',
      textSecondary: '#5c4a52',
      accentPrimary: '#dc2626',
      accentSecondary: '#b91c1c',
      textOnAccent: '#ffffff',
      badgeFav: '#eab308',
      pillBg: '#eae0e3',
      isLight: true,
    },
    green: {
      bgMain: '#f3f8f5',
      bgMainRgb: [243, 248, 245],
      bgTopBar: '#ffffff',
      bgBottomBar: '#ebf3ef',
      cardBg: '#e0ebe5',
      cardBgSel: '#d2e2d9',
      borderSubtle: '#c4d8cd',
      dotInactive: '#8ea89a',
      textPrimary: '#0f1e16',
      textSecondary: '#466052',
      accentPrimary: '#16a34a',
      accentSecondary: '#15803d',
      textOnAccent: '#ffffff',
      badgeFav: '#eab308',
      pillBg: '#e0ebe5',
      isLight: true,
    },
    yellow: {
      bgMain: '#f9f7f2',
      bgMainRgb: [249, 247, 242],
      bgTopBar: '#ffffff',
      bgBottomBar: '#f3f0e8',
      cardBg: '#ece7dc',
      cardBgSel: '#e2dbcd',
      borderSubtle: '#d4ccbc',
      dotInactive: '#a29a8a',
      textPrimary: '#1a160e',
      textSecondary: '#5e5644',
      accentPrimary: '#eab308',
      accentSecondary: '#a16207',
      textOnAccent: '#181406',
      badgeFav: '#eab308',
      pillBg: '#ebe5d8',
      isLight: true,
    },
  },
  gray: {
    blue: {
      bgMain: '#404650',
      bgMainRgb: [64, 70, 80],
      bgTopBar: '#323740',
      bgBottomBar: '#363b44',
      cardBg: '#525965',
      cardBgSel: '#626a78',
      borderSubtle: '#727b8a',
      dotInactive: '#8a92a0',
      textPrimary: '#f5f7fa',
      textSecondary: '#c8ced8',
      accentPrimary: '#38bdf8',
      accentSecondary: '#7dd3fc',
      textOnAccent: '#0a101a',
      badgeFav: '#facc15',
      pillBg: '#4c525e',
      isLight: false,
    },
    red: {
      bgMain: '#42444c',
      bgMainRgb: [66, 68, 76],
      bgTopBar: '#34353c',
      bgBottomBar: '#383940',
      cardBg: '#545660',
      cardBgSel: '#646672',
      borderSubtle: '#767884',
      dotInactive: '#8e909c',
      textPrimary: '#f6f5f8',
      textSecondary: '#ccc8d2',
      accentPrimary: '#ef4444',
      accentSecondary: '#fca5a5',
      textOnAccent: '#ffffff',
      badgeFav: '#facc15',
      pillBg: '#4e505a',
      isLight: false,
    },
    green: {
      bgMain: '#3e464a',
      bgMainRgb: [62, 70, 74],
      bgTopBar: '#31383c',
      bgBottomBar: '#353c40',
      cardBg: '#505a5f',
      cardBgSel: '#606c72',
      borderSubtle: '#707e84',
      dotInactive: '#88969c',
      textPrimary: '#f4f8f6',
      textSecondary: '#c6d2cd',
      accentPrimary: '#22c55e',
      accentSecondary: '#86efac',
      textOnAccent: '#0a180f',
      badgeFav: '#facc15',
      pillBg: '#4a5458',
      isLight: false,
    },
    yellow: {
      bgMain: '#44464c',
      bgMainRgb: [68, 70, 76],
      bgTopBar: '#36383d',
      bgBottomBar: '#3a3c41',
      cardBg: '#565960',
      cardBgSel: '#666971',
      borderSubtle: '#787b84',
      dotInactive: '#90939c',
      textPrimary: '#f8f7f2',
      textSecondary: '#d0cdc4',
      accentPrimary: '#facc15',
      accentSecondary: '#fde047',
      textOnAccent: '#181406',
      badgeFav: '#facc15',
      pillBg: '#50525a',
      isLight: false,
    },
  },
};

export function getConsoleTheme(
  displayMode: DisplayMode = 'dark',
  accentColor: AccentColor = 'blue'
): ConsoleThemePalette {
  const mode: DisplayMode = DISPLAY_MODE_ORDER.includes(displayMode) ? displayMode : 'dark';
  const accent = ACCENT_COLOR_ORDER.includes(accentColor) ? accentColor : 'blue';
  return THEME_MATRIX[mode][accent];
}

export const CONSOLE_THEME = THEME_MATRIX.dark.blue;

export const CATEGORY_ORDER: CategoryId[] = ['ALL', 'FAVORITES', 'RECENT'];

export const CATEGORY_LABELS: Record<CategoryId, string> = {
  ALL: 'TOUS',
  FAVORITES: 'FAV',
  RECENT: 'REC',
};

const SYSTEM_TNS_BLACKLIST = new Set([
  'ndless_resources.tns',
  'ndless_installer.tns',
  'ndless_installer_4.5.tns',
  'ndless_installer_5.2.tns',
  'ndless_installer_5.3.tns',
  'zehn_loader.tns',
  'zehn_loader_compressed.tns',
  'nlauncher.cfg.tns',
  'nlauncher.cache.tns',
  'nlauncher.profiles.tns',
  'nl_boot_guard.cfg.tns',
  'nl_active.lock.tns',
  'ndless.cfg.tns',
  'startup.tns',
  'nl_boot.tns',
  'nl_hotkey.tns',
  'n-launcher.tns',
  'nlauncher.tns',
  'n_launcher.tns',
]);

export function getBasename(filepath: string): string {
  const parts = filepath.replace(/\\/g, '/').split('/');
  return parts[parts.length - 1] || filepath;
}

export function computeGameMonogram(title: string): string {
  if (!title) return 'GM';
  let first = '';
  let second = '';
  for (let i = 0; i < title.length; i++) {
    const ch = title[i];
    if (/[a-zA-Z0-9]/.test(ch)) {
      if (!first) first = ch.toUpperCase();
      else {
        second = ch.toUpperCase();
        break;
      }
    }
  }
  for (let i = 1; i + 1 < title.length; i++) {
    if ((title[i] === ' ' || title[i] === '-') && /[a-zA-Z0-9]/.test(title[i + 1])) {
      second = title[i + 1].toUpperCase();
      break;
    }
  }
  const f = first || 'G';
  return `${f}${second || f}`;
}

export function isSystemOrSelfFile(
  filepath: string,
  selfPath = '/documents/ndless/N-Launcher.tns'
): boolean {
  const base = getBasename(filepath).toLowerCase();
  const selfBase = getBasename(selfPath).toLowerCase();
  if (filepath.toLowerCase() === selfPath.toLowerCase()) return true;
  if (selfBase && base === selfBase) return true;
  if (SYSTEM_TNS_BLACKLIST.has(base)) return true;
  if (base.startsWith('.')) return true;
  if (
    base.includes('ndless_installer') ||
    base.includes('ndless_resources') ||
    base.endsWith('.cfg.tns') ||
    base.endsWith('.cache.tns') ||
    base.endsWith('.profiles.tns') ||
    base.endsWith('.lock.tns') ||
    base.endsWith('.bak.tns') ||
    base.endsWith('.ico.tns') ||
    base.endsWith('.scr.tns') ||
    base.endsWith('.bmp.tns') ||
    base.endsWith('.png.tns') ||
    base.endsWith('.lua.tns') ||
    base.endsWith('.tmp.tns') ||
    base.endsWith('.sav.tns') ||
    base.endsWith('.wad.tns') ||
    base.endsWith('.pak.tns') ||
    base.endsWith('.gb.tns') ||
    base.endsWith('.gbc.tns') ||
    base.endsWith('.gba.tns') ||
    base.endsWith('.nes.tns') ||
    base.endsWith('.rom.tns')
  ) {
    return true;
  }
  return false;
}

function readU32LE(buf: Uint8Array, offset: number): number {
  if (offset + 4 > buf.length) return 0;
  return (
    (buf[offset] |
      (buf[offset + 1] << 8) |
      (buf[offset + 2] << 16) |
      (buf[offset + 3] << 24)) >>>
    0
  );
}

function readU32BE(buf: Uint8Array, offset: number): number {
  if (offset + 4 > buf.length) return 0;
  return (
    ((buf[offset] << 24) |
      (buf[offset + 1] << 16) |
      (buf[offset + 2] << 8) |
      buf[offset + 3]) >>>
    0
  );
}

function readCString(buf: Uint8Array, offset: number, maxLen: number): string {
  let out = '';
  for (let i = 0; i < maxLen && offset + i < buf.length; i++) {
    const b = buf[offset + i];
    if (b === 0) break;
    if (b >= 32 && b < 127) {
      out += String.fromCharCode(b);
    }
  }
  return out;
}

export function prettifyTitleFromFilename(filename: string): string {
  let base = getBasename(filename);
  if (base.toLowerCase().endsWith('.tns')) {
    base = base.slice(0, -4);
  }
  if (base.toLowerCase().endsWith('.prg')) {
    base = base.slice(0, -4);
  }
  base = base.replace(/_/g, ' ');
  if (base.length > 0) {
    base = base.charAt(0).toUpperCase() + base.slice(1);
  }
  return base;
}

function parseZehnAtOffset(
  buf: Uint8Array,
  zOff: number,
  totalFileSize: number,
  out: TnsInspectionResult
): boolean {
  if (zOff + 32 > buf.length) return false;
  const sig = readU32LE(buf, zOff);
  const version = readU32LE(buf, zOff + 4);
  const zehnFileSize = readU32LE(buf, zOff + 8);
  const relocCount = readU32LE(buf, zOff + 12);
  const flagCount = readU32LE(buf, zOff + 16);
  const extraSize = readU32LE(buf, zOff + 20);
  const allocSize = readU32LE(buf, zOff + 24);

  if (sig !== NL_MAGIC_ZEHN || version !== 1) return false;
  if (zehnFileSize === 0 || (totalFileSize > 0 && zehnFileSize > totalFileSize)) return false;
  if (relocCount > 65536 || flagCount > 256 || extraSize > 65536) return false;

  out.zehnOffset = zOff;
  out.zehnVersion = version;
  out.relocCount = relocCount;
  out.allocSize = allocSize;

  const flagTableOff = zOff + 32 + relocCount * 4;
  const extraDataOff = flagTableOff + flagCount * 4;

  if (flagTableOff + flagCount * 4 <= buf.length) {
    for (let i = 0; i < flagCount; i++) {
      const rawFlag = readU32LE(buf, flagTableOff + i * 4);
      const flagType = rawFlag & 0xff;
      const flagData = (rawFlag >>> 8) & 0xffffff;

      if (flagType === 0) {
        out.ndlessMin = flagData;
      } else if (flagType === 8 && extraDataOff + flagData < buf.length && flagData < extraSize) {
        const name = readCString(buf, extraDataOff + flagData, 47);
        if (name) out.execName = name;
      } else if (flagType === 9 && extraDataOff + flagData < buf.length && flagData < extraSize) {
        const author = readCString(buf, extraDataOff + flagData, 31);
        if (author) out.execAuthor = author;
      } else if (flagType === 10) {
        out.execVersion = flagData;
      } else if (flagType === 12) {
        out.supportsHww = flagData !== 0;
      } else if (flagType === 13) {
        out.usesLcdBlit = flagData !== 0;
      }
    }
  }

  return true;
}

export function inspectTnsBuffer(
  buf: Uint8Array,
  totalFileSize: number,
  filepath: string,
  selfPath = '/documents/ndless/N-Launcher.tns'
): TnsInspectionResult {
  const magic = buf.length >= 4 ? readU32LE(buf, 0) : 0;
  const magicHex =
    buf.length >= 4
      ? Array.from(buf.slice(0, 4))
          .map((b) => b.toString(16).padStart(2, '0').toUpperCase())
          .join(' ')
      : 'VIDE';

  const result: TnsInspectionResult = {
    format: 'INVALID',
    isExecutable: false,
    fileSize: totalFileSize,
    magicHex,
    zehnOffset: null,
    zehnVersion: null,
    relocCount: 0,
    allocSize: totalFileSize,
    ndlessMin: 36,
    usesLcdBlit: false,
    supportsHww: false,
    execName: '',
    execAuthor: '',
    execVersion: 1,
    reason: '',
  };

  if (!filepath.toLowerCase().endsWith('.tns') || buf.length < 16) {
    result.reason = 'Extension non-.tns ou fichier tronqué (< 16 octets)';
    return result;
  }

  if (isSystemOrSelfFile(filepath, selfPath)) {
    result.format = 'SYSTEM';
    result.reason =
      getBasename(filepath).toLowerCase() === getBasename(selfPath).toLowerCase()
        ? 'Auto-exclusion : binaire N-Launcher lui-même (argv[0])'
        : 'Exclusion système/ressource : fichier Ndless, configuration ou jaquette (.ico.tns / .bmp.tns)';
    return result;
  }

  const ascii4 = String.fromCharCode(buf[0], buf[1], buf[2], buf[3]);
  if (
    magic === NL_MAGIC_PKZIP ||
    ascii4 === '*TIM' ||
    ascii4 === '<?xm' ||
    (buf[0] === 0x0f && buf[1] === 0x0e)
  ) {
    result.format = 'TI-DOC';
    result.reason =
      'Document TI-Nspire ordinaire (archive ZIP/XML TI-Nspire OS commençant par PK\\x03\\x04) — ignoré';
    return result;
  }

  if (magic === NL_MAGIC_ZEHN) {
    if (parseZehnAtOffset(buf, 0, totalFileSize, result)) {
      result.format = 'ZEHN';
      result.isExecutable = true;
      result.reason = `Exécutable Ndless Zehn v1 natif (${result.relocCount} relocations ARM)`;
      return result;
    }
  }

  if (magic === NL_MAGIC_PRG) {
    const scanLimit = Math.min(buf.length, 2048);
    for (let off = 4; off + 32 <= scanLimit; off += 4) {
      if (readU32LE(buf, off) === NL_MAGIC_ZEHN) {
        if (parseZehnAtOffset(buf, off, totalFileSize, result)) {
          result.format = 'ZEHN';
          result.isExecutable = true;
          result.reason = `Exécutable Ndless PRG\\0 + Zehn v1 à 0x${off.toString(16)} (${result.relocCount} relocations ARM)`;
          return result;
        }
      }
    }
    result.format = 'PRG';
    result.isExecutable = true;
    result.reason = 'Exécutable ARM Ndless classique (signature PRG\\0 0x00475250)';
    return result;
  }

  if (magic === NL_MAGIC_BFLT && buf.length >= 64) {
    const rev = readU32BE(buf, 4);
    if (rev === 4) {
      result.format = 'BFLT';
      result.isExecutable = true;
      result.reason = 'Exécutable Ndless bFLT v4 valide';
      return result;
    }
  }

  result.reason = `Signature binaire inconnue (${magicHex}) — rejeté`;
  return result;
}

export function classifyGameStyle(title: string, filename: string): {
  sceneKind: GameSceneKind;
  accentHex: string;
} {
  const s = `${title} ${filename}`.toLowerCase();
  if (s.includes('doom')) return { sceneKind: 'DOOM', accentHex: '#ef4444' };
  if (s.includes('quake') || s.includes('wolf')) return { sceneKind: 'QUAKE', accentHex: '#d97706' };
  if (s.includes('gbc') || s.includes('gameboy')) return { sceneKind: 'GBC', accentHex: '#8b5cf6' };
  if (s.includes('gpsp') || s.includes('gba')) return { sceneKind: 'GBA', accentHex: '#3b82f6' };
  if (s.includes('nes') || s.includes('snes') || s.includes('pico')) {
    return { sceneKind: 'NES', accentHex: '#dc2626' };
  }
  if (s.includes('crafti') || s.includes('mine') || s.includes('terraria')) {
    return { sceneKind: 'CRAFTI', accentHex: '#10b981' };
  }
  if (s.includes('2048') || s.includes('tetris') || s.includes('chess') || s.includes('sudoku')) {
    return { sceneKind: '2048', accentHex: '#f59e0b' };
  }
  if (s.includes('pac') || s.includes('flappy') || s.includes('snake') || s.includes('pong')) {
    return { sceneKind: 'PACMAN', accentHex: '#facc15' };
  }
  if (s.includes('karuga') || s.includes('shoot') || s.includes('space') || s.includes('aster')) {
    return { sceneKind: 'SHMUP', accentHex: '#0ea5e9' };
  }
  return { sceneKind: 'RACING', accentHex: '#f97316' };
}

export function buildZehnTnsHeader(params: {
  execName: string;
  execAuthor: string;
  version: number;
  relocCount: number;
  allocSize: number;
  totalSize: number;
  usesLcdBlit: boolean;
  supportsHww: boolean;
}): Uint8Array {
  const buf = new Uint8Array(1024);
  buf[0] = 0x50;
  buf[1] = 0x52;
  buf[2] = 0x47;
  buf[3] = 0x00;
  buf[4] = 0xf0;
  buf[5] = 0x4f;
  buf[6] = 0x2d;
  buf[7] = 0xe9;

  const zOff = 0x1ec;
  const writeU32 = (off: number, val: number) => {
    buf[off] = val & 0xff;
    buf[off + 1] = (val >>> 8) & 0xff;
    buf[off + 2] = (val >>> 16) & 0xff;
    buf[off + 3] = (val >>> 24) & 0xff;
  };

  const nameBytes = new TextEncoder().encode(params.execName);
  const authBytes = new TextEncoder().encode(params.execAuthor);
  const nameLen = nameBytes.length + 1;
  const authLen = authBytes.length + 1;
  const extraSize = (nameLen + authLen + 3) & ~3;
  const flagCount = 6;
  const storedRelocs = Math.min(params.relocCount, 24);

  writeU32(zOff + 0, NL_MAGIC_ZEHN);
  writeU32(zOff + 4, 1);
  writeU32(zOff + 8, Math.max(512, params.totalSize - zOff));
  writeU32(zOff + 12, storedRelocs);
  writeU32(zOff + 16, flagCount);
  writeU32(zOff + 20, extraSize);
  writeU32(zOff + 24, params.allocSize);
  writeU32(zOff + 28, 0);

  const flagOff = zOff + 32 + storedRelocs * 4;
  writeU32(flagOff + 0, 0 | (45 << 8));
  writeU32(flagOff + 4, 8 | (0 << 8));
  writeU32(flagOff + 8, 9 | (nameLen << 8));
  writeU32(flagOff + 12, 10 | (params.version << 8));
  writeU32(flagOff + 16, 12 | ((params.supportsHww ? 1 : 0) << 8));
  writeU32(flagOff + 20, 13 | ((params.usesLcdBlit ? 1 : 0) << 8));

  const extraOff = flagOff + flagCount * 4;
  buf.set(nameBytes, extraOff);
  buf[extraOff + nameBytes.length] = 0;
  buf.set(authBytes, extraOff + nameLen);
  buf[extraOff + nameLen + authBytes.length] = 0;

  return buf;
}

export function buildTiDocZipHeader(): Uint8Array {
  const buf = new Uint8Array(64);
  const bytes = [
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0x21, 0x00, 0x00, 0x00, 0x44, 0x6f,
    0x63, 0x75, 0x6d, 0x65, 0x6e, 0x74, 0x2e, 0x78, 0x6d, 0x6c,
  ];
  buf.set(bytes, 0);
  return buf;
}

export function createInitialVirtualFilesystem(): VirtualFileEntry[] {
  return [
    {
      path: '/documents/games/fps/nDoom.tns',
      sizeBytes: 145408,
      headerBytes: buildZehnTnsHeader({
        execName: 'nDoom',
        execAuthor: 'Mrakoplaz',
        version: 3,
        relocCount: 24,
        allocSize: 524288,
        totalSize: 145408,
        usesLcdBlit: false,
        supportsHww: false,
      }),
    },
    {
      path: '/documents/games/fps/nQuake.tns',
      sizeBytes: 204800,
      headerBytes: buildZehnTnsHeader({
        execName: 'nQuake',
        execAuthor: 'R.G. / Vogtinator',
        version: 2,
        relocCount: 24,
        allocSize: 786432,
        totalSize: 204800,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/games/voxel/Crafti.tns',
      sizeBytes: 118784,
      headerBytes: buildZehnTnsHeader({
        execName: 'Crafti',
        execAuthor: 'Vogtinator',
        version: 2,
        relocCount: 24,
        allocSize: 412000,
        totalSize: 118784,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/games/arcade/nKaruga.tns',
      sizeBytes: 68608,
      headerBytes: buildZehnTnsHeader({
        execName: 'nKaruga',
        execAuthor: 'Matrefeytontias',
        version: 1,
        relocCount: 20,
        allocSize: 215040,
        totalSize: 68608,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/games/arcade/2048-CX.tns',
      sizeBytes: 29696,
      headerBytes: buildZehnTnsHeader({
        execName: '2048 CX',
        execAuthor: 'Vogtinator',
        version: 1,
        relocCount: 16,
        allocSize: 184320,
        totalSize: 29696,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/games/arcade/PacMan-CX.tns',
      sizeBytes: 36864,
      headerBytes: buildZehnTnsHeader({
        execName: 'Pac-Man CX',
        execAuthor: 'Homebrew Team',
        version: 1,
        relocCount: 18,
        allocSize: 196608,
        totalSize: 36864,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/ndless/emulators/gbc4nspire.tns',
      sizeBytes: 92160,
      headerBytes: buildZehnTnsHeader({
        execName: 'gbc4nspire',
        execAuthor: 'calc84maniac',
        version: 2,
        relocCount: 22,
        allocSize: 340000,
        totalSize: 92160,
        usesLcdBlit: false,
        supportsHww: false,
      }),
    },
    {
      path: '/documents/ndless/emulators/gpSP-Nspire.tns',
      sizeBytes: 172032,
      headerBytes: buildZehnTnsHeader({
        execName: 'gpSP GBA',
        execAuthor: 'calc84maniac',
        version: 3,
        relocCount: 24,
        allocSize: 640000,
        totalSize: 172032,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/roms/NESpire.tns',
      sizeBytes: 79872,
      headerBytes: buildZehnTnsHeader({
        execName: 'NESpire',
        execAuthor: 'NspireDev',
        version: 1,
        relocCount: 19,
        allocSize: 290000,
        totalSize: 79872,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    // Duplicate copy of nDoom.tns -> deduplicated
    {
      path: '/documents/ndless/backup/nDoom.tns',
      sizeBytes: 145408,
      headerBytes: buildZehnTnsHeader({
        execName: 'nDoom',
        execAuthor: 'Mrakoplaz',
        version: 3,
        relocCount: 24,
        allocSize: 524288,
        totalSize: 145408,
        usesLcdBlit: false,
        supportsHww: false,
      }),
    },
    // System files & N-Launcher v3 itself -> excluded
    {
      path: '/documents/ndless/ndless_resources.tns',
      sizeBytes: 54812,
      headerBytes: buildZehnTnsHeader({
        execName: 'ndless_resources',
        execAuthor: 'Ndless Team',
        version: 45,
        relocCount: 12,
        allocSize: 120000,
        totalSize: 54812,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/ndless/ndless_installer_4.5.tns',
      sizeBytes: 24108,
      headerBytes: buildZehnTnsHeader({
        execName: 'ndless_installer',
        execAuthor: 'Ndless Team',
        version: 45,
        relocCount: 8,
        allocSize: 64000,
        totalSize: 24108,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    {
      path: '/documents/ndless/N-Launcher.tns',
      sizeBytes: 94496,
      headerBytes: buildZehnTnsHeader({
        execName: 'N-Launcher',
        execAuthor: 'daniel.b',
        version: 3,
        relocCount: 24,
        allocSize: 94244,
        totalSize: 94496,
        usesLcdBlit: true,
        supportsHww: true,
      }),
    },
    // Ordinary TI-Nspire math/science course documents (.tns ZIP/XML) -> rejected
    {
      path: '/documents/Lycee/Analyse_Suites_Limites.tns',
      sizeBytes: 18432,
      headerBytes: buildTiDocZipHeader(),
    },
    {
      path: '/documents/Lycee/Physique_Ondes_Mecaniques.tns',
      sizeBytes: 24576,
      headerBytes: buildTiDocZipHeader(),
    },
    {
      path: '/documents/Lycee/Geometrie_Espace_Vecteurs.tns',
      sizeBytes: 15360,
      headerBytes: buildTiDocZipHeader(),
    },
  ];
}

export interface SavedGamePref {
  isFavorite?: boolean;
  lastPlayedRank?: number;
  launchCount?: number;
  playTimeSec?: number;
  lastPlayedTs?: number;
  shortcutKey?: string;
}

export interface ScanPreferences {
  displayMode?: DisplayMode;
  accentColor?: AccentColor;
  settings?: Partial<LauncherSettings>;
  sys?: Partial<SysEnvSim>;
  gamePrefs?: Record<string, SavedGamePref>;
  profiles?: Partial<ProfileSystemState>;
  diagEntries?: DiagEntry[];
}

export function getLayoutMetrics(
  layout: LayoutMode,
  showNames: boolean,
  showShortcuts = true,
  itemCount = 0,
  scrollDir: ScrollDir = 'vertical'
): LayoutMetrics {
  let cols = NL_GRID_COLS;
  let rows = NL_GRID_ROWS;
  let cardW = NL_GRID_CARD_W;
  let cardH = NL_GRID_CARD_H;
  let radius = NL_COVER_RADIUS;
  let marginX = NL_GRID_MARGIN_X;
  let gapX = NL_GRID_GAP_X;
  let topY = NL_GRID_TOP_Y;
  let gapY = NL_GRID_GAP_Y;
  let labelH = 0;
  const viewBottom = showShortcuts ? NL_VIEW_BOTTOM : NL_VIEW_BOTTOM_NO_SC;
  const paginationY = showShortcuts ? NL_PAGINATION_Y : NL_PAGINATION_Y_NO_SC;

  switch (layout) {
    case '1x1':
      cols = 1;
      rows = 1;
      if (showShortcuts) {
        cardW = showNames ? 152 : 164;
        cardH = cardW;
        topY = 33;
        gapY = 12;
        radius = showNames ? 17 : 18;
      } else {
        cardW = showNames ? 166 : 178;
        cardH = cardW;
        topY = 35;
        gapY = 14;
        radius = showNames ? 18 : 19;
      }
      marginX = Math.floor((NL_SCREEN_W - cardW) / 2);
      gapX = 0;
      labelH = showNames ? 12 : 0;
      break;
    case '2x2':
      cols = 2;
      rows = 2;
      if (showShortcuts) {
        cardW = showNames ? 76 : 85;
        cardH = cardW;
        gapX = 30;
        topY = 28;
        gapY = showNames ? 3 : 5;
        radius = showNames ? 10 : 11;
      } else {
        cardW = showNames ? 84 : 93;
        cardH = cardW;
        gapX = 28;
        topY = 28;
        gapY = showNames ? 4 : 6;
        radius = showNames ? 11 : 12;
      }
      marginX = Math.floor((NL_SCREEN_W - (2 * cardW + gapX)) / 2);
      labelH = showNames ? 10 : 0;
      break;
    case '3x3':
      cols = 3;
      rows = 3;
      if (showShortcuts) {
        cardW = showNames ? 48 : 56;
        cardH = cardW;
        gapX = showNames ? 24 : 22;
        topY = 28;
        gapY = showNames ? 2 : 4;
        radius = showNames ? 7 : 8;
      } else {
        cardW = showNames ? 53 : 61;
        cardH = cardW;
        gapX = showNames ? 24 : 22;
        topY = 28;
        gapY = showNames ? 3 : 5;
        radius = showNames ? 8 : 9;
      }
      marginX = Math.floor((NL_SCREEN_W - (3 * cardW + 2 * gapX)) / 2);
      labelH = showNames ? 9 : 0;
      break;
    case '4x4':
      cols = 4;
      rows = 4;
      if (showShortcuts) {
        cardW = showNames ? 34 : 42;
        cardH = cardW;
        gapX = showNames ? 22 : 20;
        topY = 28;
        gapY = showNames ? 2 : 3;
        radius = 6;
      } else {
        cardW = showNames ? 39 : 46;
        cardH = cardW;
        gapX = showNames ? 22 : 20;
        topY = 28;
        gapY = showNames ? 2 : 3;
        radius = showNames ? 6 : 7;
      }
      marginX = Math.floor((NL_SCREEN_W - (4 * cardW + 3 * gapX)) / 2);
      labelH = showNames ? 8 : 0;
      break;
    case '3x2':
    default:
      cols = NL_GRID_COLS;
      rows = NL_GRID_ROWS;
      if (showShortcuts) {
        if (!showNames) {
          cardW = NL_GRID_CARD_W;
          cardH = NL_GRID_CARD_H;
          radius = NL_COVER_RADIUS;
          marginX = NL_GRID_MARGIN_X;
          gapX = NL_GRID_GAP_X;
          topY = NL_GRID_TOP_Y;
          gapY = NL_GRID_GAP_Y;
          labelH = 0;
        } else {
          cardW = 76;
          cardH = 76;
          radius = 10;
          gapX = 22;
          marginX = Math.floor((NL_SCREEN_W - (3 * 76 + 2 * 22)) / 2);
          topY = 29;
          gapY = 3;
          labelH = 10;
        }
      } else {
        if (!showNames) {
          cardW = 90;
          cardH = 90;
          radius = 12;
          gapX = 14;
          marginX = Math.floor((NL_SCREEN_W - (3 * 90 + 2 * 14)) / 2);
          topY = 29;
          gapY = 8;
          labelH = 0;
        } else {
          cardW = 82;
          cardH = 82;
          radius = 11;
          gapX = 18;
          marginX = Math.floor((NL_SCREEN_W - (3 * 82 + 2 * 18)) / 2);
          topY = 29;
          gapY = 6;
          labelH = 10;
        }
      }
      break;
  }

  const perPage = cols * rows;

  /* Adaptive single-page vertical & horizontal optimization when active rows < max rows */
  if (itemCount > 0 && itemCount <= perPage) {
    const visRows = Math.floor((itemCount + cols - 1) / cols);
    if (visRows >= 1 && visRows < rows) {
      const availH = viewBottom - NL_VIEW_TOP;
      if (layout === '4x4') {
        if (visRows === 3) {
          if (showShortcuts) {
            cardW = showNames ? 49 : 56;
            gapX = showNames ? 18 : 14;
            gapY = showNames ? 3 : 4;
            radius = showNames ? 7 : 8;
          } else {
            cardW = showNames ? 53 : 60;
            gapX = showNames ? 16 : 14;
            gapY = showNames ? 4 : 6;
            radius = showNames ? 8 : 9;
          }
        } else if (visRows === 2) {
          if (showShortcuts) {
            cardW = showNames ? 60 : 64;
            gapX = showNames ? 14 : 12;
            gapY = showNames ? 10 : 14;
            radius = showNames ? 8 : 9;
          } else {
            cardW = showNames ? 62 : 66;
            gapX = showNames ? 14 : 12;
            gapY = showNames ? 12 : 16;
            radius = 9;
          }
        } else {
          cardW = showNames ? 64 : 68;
          gapX = showNames ? 12 : 10;
          gapY = 8;
          radius = showNames ? 9 : 10;
        }
        cardH = cardW;
        marginX = Math.floor((NL_SCREEN_W - (4 * cardW + 3 * gapX)) / 2);
      } else if (layout === '3x3') {
        if (visRows === 2) {
          if (showShortcuts) {
            cardW = showNames ? 74 : 80;
            gapX = showNames ? 20 : 18;
            gapY = showNames ? 6 : 8;
            radius = showNames ? 10 : 11;
          } else {
            cardW = showNames ? 80 : 86;
            gapX = showNames ? 18 : 16;
            gapY = showNames ? 8 : 10;
            radius = 11;
          }
        } else {
          if (showShortcuts) {
            cardW = showNames ? 84 : 88;
            gapX = showNames ? 16 : 14;
            gapY = 8;
            radius = showNames ? 11 : 12;
          } else {
            cardW = showNames ? 88 : 92;
            gapX = showNames ? 14 : 12;
            gapY = 8;
            radius = 12;
          }
        }
        cardH = cardW;
        marginX = Math.floor((NL_SCREEN_W - (3 * cardW + 2 * gapX)) / 2);
      } else if (layout === '3x2') {
        if (showShortcuts) {
          cardW = showNames ? 84 : 88;
          gapX = showNames ? 16 : 14;
          gapY = 8;
          radius = showNames ? 11 : 12;
        } else {
          cardW = showNames ? 88 : 92;
          gapX = showNames ? 14 : 12;
          gapY = 8;
          radius = 12;
        }
        cardH = cardW;
        marginX = Math.floor((NL_SCREEN_W - (3 * cardW + 2 * gapX)) / 2);
      } else if (layout === '2x2') {
        if (showShortcuts) {
          cardW = showNames ? 108 : 116;
          gapX = showNames ? 26 : 24;
          gapY = 8;
          radius = showNames ? 13 : 14;
        } else {
          cardW = showNames ? 116 : 124;
          gapX = showNames ? 26 : 24;
          gapY = 8;
          radius = showNames ? 14 : 15;
        }
        cardH = cardW;
        marginX = Math.floor((NL_SCREEN_W - (2 * cardW + gapX)) / 2);
      }
      const contentH = visRows * (cardH + labelH) + (visRows - 1) * gapY;
      topY = Math.max(NL_VIEW_TOP + 2, NL_VIEW_TOP + Math.floor((availH - contentH) / 2));
    }
  }

  const isHorizontal = scrollDir === 'horizontal';

  /* R15 Horizontal mode (exact reference): guarantee >= 28 px side margin for 12 px left & right icon peeks.
   * R17 Vertical mode (exact mirror of R15 horizontal): guarantee >= 20 px top & bottom margin in multi-page mode
   * so neighboring pages' real icons peek naturally by 12 px at top & bottom with >= 8 px inter-page gap. */
  if (isHorizontal) {
    if (marginX < 28 && cols > 1) {
      const maxCw = NL_SCREEN_W - 56;
      let contentW = cols * cardW + (cols - 1) * gapX;
      while (contentW > maxCw && gapX > 6) {
        gapX--;
        contentW = cols * cardW + (cols - 1) * gapX;
      }
      while (contentW > maxCw && cardW > 24) {
        cardW--;
        cardH = cardW;
        contentW = cols * cardW + (cols - 1) * gapX;
      }
      marginX = Math.floor((NL_SCREEN_W - contentW) / 2);
    }
  } else if (itemCount <= 0 || itemCount > perPage) {
    const availH = viewBottom - NL_VIEW_TOP;
    const maxCh = availH - 40; /* 20 px top + 20 px bottom => 12 px peek + 8 px inter-page gap */
    const minGapY =
      rows <= 2 ? (showNames ? 3 : 5) : rows === 3 ? (showNames ? 2 : 4) : 2;
    let contentH = rows * (cardH + labelH) + (rows - 1) * gapY;
    while (contentH > maxCh && gapY > minGapY) {
      gapY--;
      contentH = rows * (cardH + labelH) + (rows - 1) * gapY;
    }
    while (contentH > maxCh && cardH > 24) {
      cardH--;
      cardW = cardH;
      contentH = rows * (cardH + labelH) + (rows - 1) * gapY;
    }
    if (cols > 1) {
      const contentW = cols * cardW + (cols - 1) * gapX;
      marginX = Math.floor((NL_SCREEN_W - contentW) / 2);
    } else {
      marginX = Math.floor((NL_SCREEN_W - cardW) / 2);
    }
    topY = NL_VIEW_TOP + Math.floor((availH - contentH) / 2);
  }

  const rowStrideY = cardH + labelH + gapY;
  let pageStrideY: number;
  let pageStrideX: number;
  if (isHorizontal) {
    pageStrideY = rowStrideY * rows;
    const contentW = cols * cardW + (cols - 1) * gapX;
    pageStrideX = Math.max(contentW + 16, NL_SCREEN_W - 12 - marginX);
  } else {
    if (itemCount <= 0 || itemCount > perPage) {
      const contentH = rows * (cardH + labelH) + (rows - 1) * gapY;
      pageStrideY = Math.max(contentH + 8, viewBottom - 12 - topY);
    } else {
      pageStrideY = rowStrideY * rows;
    }
    pageStrideX = NL_SCREEN_W;
  }

  return {
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
    pageStrideX,
    isHorizontal,
    viewBottom,
    paginationY,
  };
}

export function computeGridSlotPos(
  m: LayoutMetrics,
  filteredCount: number,
  fIdx: number
): { x: number; y: number } {
  const cols = m.cols > 0 ? m.cols : 1;
  const perPage = m.perPage > 0 ? m.perPage : cols;
  const page = fIdx >= 0 ? Math.floor(fIdx / perPage) : 0;
  const slot = fIdx >= 0 ? fIdx % perPage : 0;
  const rowInPage = Math.floor(slot / cols);
  const col = slot % cols;

  const globalRow = fIdx >= 0 ? Math.floor(fIdx / cols) : 0;
  const rowStart = globalRow * cols;
  const itemsInRow = Math.max(1, Math.min(cols, filteredCount - rowStart));

  let x: number;
  if (itemsInRow < cols) {
    const rowW = itemsInRow * m.cardW + (itemsInRow - 1) * m.gapX;
    const rowMarginX = Math.floor((NL_SCREEN_W - rowW) / 2);
    x = rowMarginX + col * (m.cardW + m.gapX);
  } else {
    x = m.marginX + col * (m.cardW + m.gapX);
  }

  if (m.isHorizontal) {
    x += page * m.pageStrideX;
    let pageTopY = m.topY;
    if (filteredCount > perPage) {
      const pageItems = Math.max(1, Math.min(perPage, filteredCount - page * perPage));
      const rowsOnPage = Math.floor((pageItems + cols - 1) / cols);
      if (rowsOnPage < m.rows) {
        const availH = m.viewBottom - NL_VIEW_TOP;
        const contentH = rowsOnPage * (m.cardH + m.labelH) + (rowsOnPage - 1) * m.gapY;
        const centeredY = NL_VIEW_TOP + Math.floor((availH - contentH) / 2);
        if (centeredY > pageTopY) {
          pageTopY = centeredY;
        }
      }
    }
    return { x, y: pageTopY + rowInPage * m.rowStrideY };
  }

  /* Vertical mode: align every page cleanly to m.topY so even partial final pages peek cleanly at bottom */
  const y = m.topY + page * m.pageStrideY + rowInPage * m.rowStrideY;
  return { x, y };
}

export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  if (s === 0) return '0s';
  if (s < 60) return `${s}s`;
  if (s < 3600) {
    const m = Math.floor(s / 60);
    const rem = s % 60;
    return `${m}m ${String(rem).padStart(2, '0')}s`;
  }
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  return `${h}h ${String(m).padStart(2, '0')}m`;
}

export function formatTimestamp(epochSec: number): string {
  if (!epochSec || epochSec < 946684800) return '--';
  const d = new Date(epochSec * 1000);
  const dd = String(d.getUTCDate()).padStart(2, '0');
  const mm = String(d.getUTCMonth() + 1).padStart(2, '0');
  const yy = String(d.getUTCFullYear() % 100).padStart(2, '0');
  const hh = String(d.getUTCHours()).padStart(2, '0');
  const min = String(d.getUTCMinutes()).padStart(2, '0');
  return `${dd}/${mm}/${yy} ${hh}:${min}`;
}

export function isShortcutReserved(ch: string): boolean {
  if (!ch) return false;
  const u = ch.toUpperCase();
  return [
    '0',
    '.',
    '-',
    '2',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    '=',
    '*',
    '+',
    '\x1b',
    '\n',
    '\r',
    '\t',
  ].includes(u);
}

export function isShortcutAssignable(ch: string): boolean {
  if (!ch) return false;
  const u = ch.toUpperCase();
  if (isShortcutReserved(u)) return false;
  return ASSIGNABLE_SHORTCUT_KEYS.includes(u);
}

export function findShortcutOwner(
  state: LauncherState,
  ch: string,
  excludeGameIdx = -1
): number {
  if (!ch) return -1;
  const u = ch.toUpperCase();
  for (let i = 0; i < state.games.length; i++) {
    if (i === excludeGameIdx) continue;
    if (state.games[i].shortcutKey && state.games[i].shortcutKey.toUpperCase() === u) {
      return i;
    }
  }
  return -1;
}

export function extractGameStem(filename: string): string {
  const base = getBasename(filename);
  const dotIdx = base.toLowerCase().lastIndexOf('.tns');
  return dotIdx > 0 ? base.slice(0, dotIdx) : base;
}

export interface DecodedIcoResult {
  width: number;
  height: number;
  sourceWidth: number;
  sourceHeight: number;
  bpp: number;
  entryCount: number;
  rgb565Pixels: Uint16Array;
  dataUrl: string;
}

const PNG_CRC32_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    }
    table[n] = c >>> 0;
  }
  return table;
})();

export function crc32Bytes(buf: Uint8Array): number {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) {
    c = PNG_CRC32_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

export function computeLibraryCacheCrc32(games: GameEntry[]): number {
  const summary = games
    .map(
      (g) =>
        `${g.path}|${g.title}|${g.author}|${g.sizeBytes}|${g.binFormat}|${g.iconSource}|${
          g.capturedIconDataUrl ? g.capturedIconDataUrl.length : 0
        }`
    )
    .join('\n');
  return crc32Bytes(new TextEncoder().encode(summary));
}

function makePngChunk(type: string, data: Uint8Array): Uint8Array {
  const out = new Uint8Array(12 + data.length);
  const len = data.length;
  out[0] = (len >>> 24) & 0xff;
  out[1] = (len >>> 16) & 0xff;
  out[2] = (len >>> 8) & 0xff;
  out[3] = len & 0xff;
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  const crc = crc32Bytes(out.subarray(4, 8 + data.length));
  const crcOff = 8 + data.length;
  out[crcOff] = (crc >>> 24) & 0xff;
  out[crcOff + 1] = (crc >>> 16) & 0xff;
  out[crcOff + 2] = (crc >>> 8) & 0xff;
  out[crcOff + 3] = crc & 0xff;
  return out;
}

function encodeRgbaToRawPngBytes(rgba: Uint8Array, w: number, h: number): Uint8Array {
  const rawStride = 1 + w * 4;
  const raw = new Uint8Array(rawStride * h);
  for (let y = 0; y < h; y++) {
    const rowOff = y * rawStride;
    raw[rowOff] = 0; // filter None
    raw.set(rgba.subarray(y * w * 4, (y + 1) * w * 4), rowOff + 1);
  }

  let s1 = 1;
  let s2 = 0;
  for (let i = 0; i < raw.length; i++) {
    s1 = (s1 + raw[i]) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  const adler = ((s2 << 16) | s1) >>> 0;

  const maxBlock = 65535;
  const numBlocks = Math.max(1, Math.ceil(raw.length / maxBlock));
  const zlibBuf = new Uint8Array(2 + numBlocks * 5 + raw.length + 4);
  zlibBuf[0] = 0x78;
  zlibBuf[1] = 0x01;
  let zPos = 2;
  let rOff = 0;
  for (let b = 0; b < numBlocks; b++) {
    const chunkLen = Math.min(maxBlock, raw.length - rOff);
    const isLast = rOff + chunkLen >= raw.length ? 1 : 0;
    zlibBuf[zPos++] = isLast;
    zlibBuf[zPos++] = chunkLen & 0xff;
    zlibBuf[zPos++] = (chunkLen >>> 8) & 0xff;
    const nlen = (~chunkLen) & 0xffff;
    zlibBuf[zPos++] = nlen & 0xff;
    zlibBuf[zPos++] = (nlen >>> 8) & 0xff;
    zlibBuf.set(raw.subarray(rOff, rOff + chunkLen), zPos);
    zPos += chunkLen;
    rOff += chunkLen;
  }
  zlibBuf[zPos++] = (adler >>> 24) & 0xff;
  zlibBuf[zPos++] = (adler >>> 16) & 0xff;
  zlibBuf[zPos++] = (adler >>> 8) & 0xff;
  zlibBuf[zPos++] = adler & 0xff;

  const ihdrData = new Uint8Array(13);
  ihdrData[0] = (w >>> 24) & 0xff;
  ihdrData[1] = (w >>> 16) & 0xff;
  ihdrData[2] = (w >>> 8) & 0xff;
  ihdrData[3] = w & 0xff;
  ihdrData[4] = (h >>> 24) & 0xff;
  ihdrData[5] = (h >>> 16) & 0xff;
  ihdrData[6] = (h >>> 8) & 0xff;
  ihdrData[7] = h & 0xff;
  ihdrData[8] = 8; // bit depth
  ihdrData[9] = 6; // RGBA
  ihdrData[10] = 0;
  ihdrData[11] = 0;
  ihdrData[12] = 0;

  const sig = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const ihdr = makePngChunk('IHDR', ihdrData);
  const idat = makePngChunk('IDAT', zlibBuf.subarray(0, zPos));
  const iend = makePngChunk('IEND', new Uint8Array(0));

  const out = new Uint8Array(sig.length + ihdr.length + idat.length + iend.length);
  let o = 0;
  out.set(sig, o);
  o += sig.length;
  out.set(ihdr, o);
  o += ihdr.length;
  out.set(idat, o);
  o += idat.length;
  out.set(iend, o);
  return out;
}

export function encodeRgbaToPngDataUrl(rgba: Uint8Array, w: number, h: number): string {
  const out = encodeRgbaToRawPngBytes(rgba, w, h);
  let binary = '';
  for (let i = 0; i < out.length; i++) {
    binary += String.fromCharCode(out[i]);
  }
  const b64 =
    typeof btoa === 'function'
      ? btoa(binary)
      : Buffer.from(out.buffer, out.byteOffset, out.byteLength).toString('base64');
  return `data:image/png;base64,${b64}`;
}

export function computeIconFitDims(srcW: number, srcH: number): { width: number; height: number } {
  if (srcW <= NL_ICON_MAX_DIM && srcH <= NL_ICON_MAX_DIM) {
    return { width: srcW, height: srcH };
  }
  if (srcW >= srcH) {
    const outW = NL_ICON_MAX_DIM;
    const outH = Math.max(1, Math.min(NL_ICON_MAX_DIM, Math.floor((srcH * NL_ICON_MAX_DIM + Math.floor(srcW / 2)) / srcW)));
    return { width: outW, height: outH };
  }
  const outH = NL_ICON_MAX_DIM;
  const outW = Math.max(1, Math.min(NL_ICON_MAX_DIM, Math.floor((srcW * NL_ICON_MAX_DIM + Math.floor(srcH / 2)) / srcH)));
  return { width: outW, height: outH };
}

const DEFLATE_LEN_BASE = [
  3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
  163, 195, 227, 258,
];
const DEFLATE_LEN_EXTRA = [
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
];
const DEFLATE_DIST_BASE = [
  1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049,
  3073, 4097, 6145, 8193, 12289, 16385, 24577,
];
const DEFLATE_DIST_EXTRA = [
  0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
];
const DEFLATE_CL_ORDER = [16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15];

interface HuffTable {
  counts: Uint16Array;
  symbols: Uint16Array;
}

function buildHuffTable(lens: Uint8Array, offset: number, numSyms: number): HuffTable | null {
  const counts = new Uint16Array(16);
  const symbols = new Uint16Array(Math.max(288, numSyms));
  const offs = new Uint16Array(16);
  for (let i = 0; i < numSyms; i++) {
    const l = lens[offset + i];
    if (l >= 16) return null;
    counts[l]++;
  }
  if (counts[0] === numSyms) {
    return { counts, symbols };
  }
  let left = 1;
  for (let len = 1; len <= 15; len++) {
    left <<= 1;
    left -= counts[len];
    if (left < 0) return null;
  }
  offs[1] = 0;
  for (let len = 1; len < 15; len++) {
    offs[len + 1] = offs[len] + counts[len];
  }
  for (let sym = 0; sym < numSyms; sym++) {
    const l = lens[offset + sym];
    if (l !== 0) {
      symbols[offs[l]++] = sym;
    }
  }
  return { counts, symbols };
}

function inflateZlibSync(src: Uint8Array, maxOutCap: number): Uint8Array | null {
  if (src.length < 6) return null;
  const cmf = src[0];
  const flg = src[1];
  if ((cmf & 0x0f) !== 8) return null;
  if ((cmf * 256 + flg) % 31 !== 0) return null;
  const hdrOff = flg & 0x20 ? 6 : 2;
  if (hdrOff >= src.length) return null;

  let pos = hdrOff;
  let bitBuf = 0;
  let bitCnt = 0;

  const readBits = (n: number): number => {
    while (bitCnt < n) {
      if (pos >= src.length) return -1;
      bitBuf |= src[pos++] << bitCnt;
      bitCnt += 8;
    }
    const v = bitBuf & ((1 << n) - 1);
    bitBuf >>>= n;
    bitCnt -= n;
    return v;
  };

  const decodeSym = (h: HuffTable): number => {
    let code = 0;
    let first = 0;
    let index = 0;
    for (let len = 1; len <= 15; len++) {
      const bit = readBits(1);
      if (bit < 0) return -1;
      code |= bit;
      const count = h.counts[len];
      if (code - count < first) {
        return h.symbols[index + (code - first)];
      }
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  };

  const dst = new Uint8Array(maxOutCap);
  let outPos = 0;
  let bfinal = 0;

  do {
    bfinal = readBits(1);
    const btype = readBits(2);
    if (bfinal < 0 || btype < 0) return null;

    if (btype === 0) {
      bitBuf = 0;
      bitCnt = 0;
      if (pos + 4 > src.length) return null;
      const blen = src[pos] | (src[pos + 1] << 8);
      const bnlen = src[pos + 2] | (src[pos + 3] << 8);
      pos += 4;
      if (blen + bnlen !== 0xffff) return null;
      if (pos + blen > src.length || outPos + blen > dst.length) return null;
      dst.set(src.subarray(pos, pos + blen), outPos);
      pos += blen;
      outPos += blen;
    } else if (btype === 1 || btype === 2) {
      let litH: HuffTable | null = null;
      let distH: HuffTable | null = null;
      const lengths = new Uint8Array(288 + 32);

      if (btype === 1) {
        for (let i = 0; i <= 143; i++) lengths[i] = 8;
        for (let i = 144; i <= 255; i++) lengths[i] = 9;
        for (let i = 256; i <= 279; i++) lengths[i] = 7;
        for (let i = 280; i <= 287; i++) lengths[i] = 8;
        for (let i = 0; i < 32; i++) lengths[288 + i] = 5;
        litH = buildHuffTable(lengths, 0, 288);
        distH = buildHuffTable(lengths, 288, 32);
      } else {
        const hlitR = readBits(5);
        const hdistR = readBits(5);
        const hclenR = readBits(4);
        if (hlitR < 0 || hdistR < 0 || hclenR < 0) return null;
        const hlit = hlitR + 257;
        const hdist = hdistR + 1;
        const hclen = hclenR + 4;
        if (hlit > 286 || hdist > 30) return null;

        const clLens = new Uint8Array(19);
        for (let i = 0; i < hclen; i++) {
          const v = readBits(3);
          if (v < 0) return null;
          clLens[DEFLATE_CL_ORDER[i]] = v;
        }
        const clH = buildHuffTable(clLens, 0, 19);
        if (!clH) return null;

        const totalCodes = hlit + hdist;
        let idx = 0;
        while (idx < totalCodes) {
          const sym = decodeSym(clH);
          if (sym < 0) return null;
          if (sym < 16) {
            lengths[idx++] = sym;
          } else {
            let repVal = 0;
            let repCnt = 0;
            if (sym === 16) {
              const ex = readBits(2);
              if (idx === 0 || ex < 0) return null;
              repVal = lengths[idx - 1];
              repCnt = 3 + ex;
            } else if (sym === 17) {
              const ex = readBits(3);
              if (ex < 0) return null;
              repCnt = 3 + ex;
            } else {
              const ex = readBits(7);
              if (ex < 0) return null;
              repCnt = 11 + ex;
            }
            if (idx + repCnt > totalCodes) return null;
            for (let r = 0; r < repCnt; r++) lengths[idx++] = repVal;
          }
        }
        litH = buildHuffTable(lengths, 0, hlit);
        distH = buildHuffTable(lengths, hlit, hdist);
      }

      if (!litH || !distH) return null;

      for (;;) {
        const sym = decodeSym(litH);
        if (sym < 0) return null;
        if (sym < 256) {
          if (outPos >= dst.length) return null;
          dst[outPos++] = sym;
        } else if (sym === 256) {
          break;
        } else {
          const lidx = sym - 257;
          if (lidx < 0 || lidx >= 29) return null;
          const lex = DEFLATE_LEN_EXTRA[lidx] > 0 ? readBits(DEFLATE_LEN_EXTRA[lidx]) : 0;
          if (lex < 0) return null;
          const mlen = DEFLATE_LEN_BASE[lidx] + lex;

          const dsym = decodeSym(distH);
          if (dsym < 0 || dsym >= 30) return null;
          const dex = DEFLATE_DIST_EXTRA[dsym] > 0 ? readBits(DEFLATE_DIST_EXTRA[dsym]) : 0;
          if (dex < 0) return null;
          const mdist = DEFLATE_DIST_BASE[dsym] + dex;
          if (mdist <= 0 || mdist > outPos || outPos + mlen > dst.length) return null;
          for (let k = 0; k < mlen; k++) {
            dst[outPos] = dst[outPos - mdist];
            outPos++;
          }
        }
      }
    } else {
      return null;
    }
  } while (!bfinal);

  return dst.subarray(0, outPos);
}

function pngPaeth(a: number, b: number, c: number): number {
  const p = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  if (pb <= pc) return b;
  return c;
}

function pngUnfilterPass(raw: Uint8Array, rawOff: number, ph: number, bppBytes: number, stride: number): boolean {
  let prevOff = -1;
  for (let y = 0; y < ph; y++) {
    const rowOff = rawOff + y * (1 + stride);
    const ftype = raw[rowOff];
    const pixOff = rowOff + 1;
    if (ftype > 4) return false;
    for (let i = 0; i < stride; i++) {
      const a = i >= bppBytes ? raw[pixOff + i - bppBytes] : 0;
      const b = prevOff >= 0 ? raw[prevOff + i] : 0;
      const c = prevOff >= 0 && i >= bppBytes ? raw[prevOff + i - bppBytes] : 0;
      if (ftype === 1) raw[pixOff + i] = (raw[pixOff + i] + a) & 0xff;
      else if (ftype === 2) raw[pixOff + i] = (raw[pixOff + i] + b) & 0xff;
      else if (ftype === 3) raw[pixOff + i] = (raw[pixOff + i] + ((a + b) >> 1)) & 0xff;
      else if (ftype === 4) raw[pixOff + i] = (raw[pixOff + i] + pngPaeth(a, b, c)) & 0xff;
    }
    prevOff = pixOff;
  }
  return true;
}

function pngSampleAt(raw: Uint8Array, rowOff: number, x: number, bitDepth: number, chIdx: number, nCh: number): number {
  if (bitDepth === 8) {
    return raw[rowOff + x * nCh + chIdx];
  }
  if (bitDepth === 16) {
    return raw[rowOff + (x * nCh + chIdx) * 2];
  }
  const bitOff = (x * nCh + chIdx) * bitDepth;
  const byteV = raw[rowOff + (bitOff >> 3)];
  const shift = 8 - bitDepth - (bitOff & 7);
  const mask = (1 << bitDepth) - 1;
  return (byteV >>> shift) & mask;
}

function decodePngPayloadToRgba(
  png: Uint8Array
): { width: number; height: number; rgba: Uint8Array } | null {
  if (
    png.length < 33 ||
    png[0] !== 0x89 ||
    png[1] !== 0x50 ||
    png[2] !== 0x4e ||
    png[3] !== 0x47 ||
    png[4] !== 0x0d ||
    png[5] !== 0x0a ||
    png[6] !== 0x1a ||
    png[7] !== 0x0a
  ) {
    return null;
  }

  let w = 0;
  let h = 0;
  let bitDepth = 0;
  let colorType = 0;
  let interlace = 0;
  let haveIhdr = false;
  let haveIend = false;

  const plte = new Uint8Array(256 * 3);
  let plteCnt = 0;
  const trnsAlpha = new Uint8Array(256).fill(255);
  let haveTrnsKey = false;
  let trnsR = 0;
  let trnsG = 0;
  let trnsB = 0;

  const idatChunks: Uint8Array[] = [];
  let idatTotalLen = 0;

  let pos = 8;
  while (pos + 12 <= png.length) {
    const clen = readU32BE(png, pos);
    const ctype = String.fromCharCode(png[pos + 4], png[pos + 5], png[pos + 6], png[pos + 7]);
    if (pos + 12 + clen > png.length) return null;
    const cdataOff = pos + 8;

    if (ctype === 'IHDR') {
      if (clen !== 13) return null;
      w = readU32BE(png, cdataOff);
      h = readU32BE(png, cdataOff + 4);
      bitDepth = png[cdataOff + 8];
      colorType = png[cdataOff + 9];
      if (w === 0 || w > 1024 || h === 0 || h > 1024) return null;
      if (png[cdataOff + 10] !== 0 || png[cdataOff + 11] !== 0 || png[cdataOff + 12] > 1) return null;
      interlace = png[cdataOff + 12];
      haveIhdr = true;
    } else if (ctype === 'PLTE') {
      if (clen % 3 !== 0 || clen > 768) return null;
      plteCnt = clen / 3;
      plte.set(png.subarray(cdataOff, cdataOff + clen), 0);
    } else if (ctype === 'tRNS') {
      if (colorType === 3) {
        const n = Math.min(256, clen);
        trnsAlpha.set(png.subarray(cdataOff, cdataOff + n), 0);
      } else if (colorType === 0 && clen >= 2) {
        haveTrnsKey = true;
        trnsR = (png[cdataOff] << 8) | png[cdataOff + 1];
      } else if (colorType === 2 && clen >= 6) {
        haveTrnsKey = true;
        trnsR = (png[cdataOff] << 8) | png[cdataOff + 1];
        trnsG = (png[cdataOff + 2] << 8) | png[cdataOff + 3];
        trnsB = (png[cdataOff + 4] << 8) | png[cdataOff + 5];
      }
    } else if (ctype === 'IDAT') {
      if (idatTotalLen + clen > 4 * 1024 * 1024) return null;
      idatChunks.push(png.subarray(cdataOff, cdataOff + clen));
      idatTotalLen += clen;
    } else if (ctype === 'IEND') {
      haveIend = true;
      break;
    }

    pos += 12 + clen;
  }

  if (!haveIhdr || !haveIend || idatTotalLen === 0) return null;

  let nCh = 0;
  if (colorType === 0) nCh = 1;
  else if (colorType === 2) nCh = 3;
  else if (colorType === 3) nCh = 1;
  else if (colorType === 4) nCh = 2;
  else if (colorType === 6) nCh = 4;
  else return null;

  if (![1, 2, 4, 8, 16].includes(bitDepth)) return null;

  const idatBuf = new Uint8Array(idatTotalLen);
  let idOff = 0;
  for (const ch of idatChunks) {
    idatBuf.set(ch, idOff);
    idOff += ch.length;
  }

  const maxRawBytes =
    h * (1 + Math.floor((w * nCh * bitDepth + 7) / 8)) + (interlace ? h * 7 + 64 : 0);
  const raw = inflateZlibSync(idatBuf, maxRawBytes);
  if (!raw) return null;

  const rgba = new Uint8Array(w * h * 4);
  const bppBytes = Math.max(1, Math.floor((nCh * bitDepth + 7) / 8));
  const a7x0 = [0, 4, 0, 2, 0, 1, 0];
  const a7y0 = [0, 0, 4, 0, 2, 0, 1];
  const a7dx = [8, 8, 4, 4, 2, 2, 1];
  const a7dy = [8, 8, 8, 4, 4, 2, 2];
  const numPasses = interlace ? 7 : 1;
  let rawOff = 0;

  for (let pass = 0; pass < numPasses; pass++) {
    const px0 = interlace ? a7x0[pass] : 0;
    const py0 = interlace ? a7y0[pass] : 0;
    const pdx = interlace ? a7dx[pass] : 1;
    const pdy = interlace ? a7dy[pass] : 1;
    const pw = w > px0 ? Math.floor((w - px0 + pdx - 1) / pdx) : 0;
    const ph = h > py0 ? Math.floor((h - py0 + pdy - 1) / pdy) : 0;
    if (pw <= 0 || ph <= 0) continue;

    const stride = Math.floor((pw * nCh * bitDepth + 7) / 8);
    const passBytes = ph * (1 + stride);
    if (rawOff + passBytes > raw.length || !pngUnfilterPass(raw, rawOff, ph, bppBytes, stride)) {
      return null;
    }

    const scaleMax = bitDepth < 8 ? (1 << bitDepth) - 1 : 255;
    for (let py = 0; py < ph; py++) {
      const rowPixOff = rawOff + py * (1 + stride) + 1;
      const dy = py0 + py * pdy;
      for (let px = 0; px < pw; px++) {
        const dx = px0 + px * pdx;
        let r = 0;
        let g = 0;
        let b = 0;
        let a = 255;

        if (colorType === 3) {
          const idx = pngSampleAt(raw, rowPixOff, px, bitDepth, 0, 1);
          if (idx < plteCnt) {
            r = plte[idx * 3];
            g = plte[idx * 3 + 1];
            b = plte[idx * 3 + 2];
            a = trnsAlpha[idx];
          }
        } else if (colorType === 0 || colorType === 4) {
          const v = pngSampleAt(raw, rowPixOff, px, bitDepth, 0, nCh);
          const g8 = bitDepth < 8 ? Math.floor((v * 255) / scaleMax) : v;
          r = g = b = g8;
          if (colorType === 4) {
            a = pngSampleAt(raw, rowPixOff, px, bitDepth, 1, 2);
          } else if (haveTrnsKey && v === (trnsR & 0xff)) {
            a = 0;
          }
        } else {
          r = pngSampleAt(raw, rowPixOff, px, bitDepth, 0, nCh);
          g = pngSampleAt(raw, rowPixOff, px, bitDepth, 1, nCh);
          b = pngSampleAt(raw, rowPixOff, px, bitDepth, 2, nCh);
          if (colorType === 6) {
            a = pngSampleAt(raw, rowPixOff, px, bitDepth, 3, 4);
          } else if (
            haveTrnsKey &&
            r === (trnsR & 0xff) &&
            g === (trnsG & 0xff) &&
            b === (trnsB & 0xff)
          ) {
            a = 0;
          }
        }

        const outOff = (dy * w + dx) * 4;
        rgba[outOff] = r;
        rgba[outOff + 1] = g;
        rgba[outOff + 2] = b;
        rgba[outOff + 3] = a;
      }
    }
    rawOff += passBytes;
  }

  return { width: w, height: h, rgba };
}

function finalizeRgbaIntoIcoResult(
  rgba: Uint8Array,
  srcW: number,
  srcH: number,
  bpp: number,
  entryCount: number,
  preserveAllZeroAlpha = false
): DecodedIcoResult {
  let anyNonzeroAlpha = false;
  let anyPartialAlpha = false;
  const totalPx = srcW * srcH;
  for (let i = 0; i < totalPx; i++) {
    const a = rgba[i * 4 + 3];
    if (a > 0) anyNonzeroAlpha = true;
    if (a < 255) anyPartialAlpha = true;
  }

  /* Independent 4-side transparent outer margin trim when artwork sits inside transparent padding */
  let cropX = 0;
  let cropY = 0;
  let cropW = srcW;
  let cropH = srcH;
  if (anyNonzeroAlpha && anyPartialAlpha && srcW >= 24 && srcH >= 24) {
    let minX = srcW;
    let minY = srcH;
    let maxX = -1;
    let maxY = -1;
    for (let y = 0; y < srcH; y++) {
      for (let x = 0; x < srcW; x++) {
        if (rgba[(y * srcW + x) * 4 + 3] > 16) {
          if (x < minX) minX = x;
          if (x > maxX) maxX = x;
          if (y < minY) minY = y;
          if (y > maxY) maxY = y;
        }
      }
    }
    if (maxX >= minX && maxY >= minY) {
      const padL = minX;
      const padR = srcW - 1 - maxX;
      const padT = minY;
      const padB = srcH - 1 - maxY;
      const x0 = padL >= 3 ? minX - 1 : 0;
      const x1 = padR >= 3 ? maxX + 1 : srcW - 1;
      const y0 = padT >= 3 ? minY - 1 : 0;
      const y1 = padB >= 3 ? maxY + 1 : srcH - 1;
      const cw = x1 - x0 + 1;
      const ch = y1 - y0 + 1;
      if (cw >= 16 && ch >= 16) {
        cropX = x0;
        cropY = y0;
        cropW = cw;
        cropH = ch;
      }
    }
  }

  const { width: dstW, height: dstH } = computeIconFitDims(cropW, cropH);
  const keepAlpha = (anyPartialAlpha && anyNonzeroAlpha) || (preserveAllZeroAlpha && !anyNonzeroAlpha);
  const rgb565Pixels = new Uint16Array(dstW * dstH);
  const outRgba = new Uint8Array(dstW * dstH * 4);

  for (let dy = 0; dy < dstH; dy++) {
    const sy0 = cropY + Math.floor((dy * cropH) / dstH);
    const sy1 = Math.min(cropY + cropH, Math.max(sy0 + 1, cropY + Math.floor(((dy + 1) * cropH + dstH - 1) / dstH)));

    for (let dx = 0; dx < dstW; dx++) {
      const sx0 = cropX + Math.floor((dx * cropW) / dstW);
      const sx1 = Math.min(cropX + cropW, Math.max(sx0 + 1, cropX + Math.floor(((dx + 1) * cropW + dstW - 1) / dstW)));

      let accR = 0;
      let accG = 0;
      let accB = 0;
      let accA = 0;
      let accN = 0;

      for (let sy = sy0; sy < sy1; sy++) {
        for (let sx = sx0; sx < sx1; sx++) {
          const pOff = (sy * srcW + sx) * 4;
          if (keepAlpha) {
            const a = rgba[pOff + 3];
            accR += rgba[pOff] * a;
            accG += rgba[pOff + 1] * a;
            accB += rgba[pOff + 2] * a;
            accA += a;
          } else {
            accR += rgba[pOff];
            accG += rgba[pOff + 1];
            accB += rgba[pOff + 2];
          }
          accN++;
        }
      }

      const cnt = accN > 0 ? accN : 1;
      let rOut = 25;
      let gOut = 32;
      let bOut = 44;
      let aOut = 255;

      if (keepAlpha) {
        aOut = Math.min(255, Math.floor((accA + Math.floor(cnt / 2)) / cnt));
        if (accA > 0) {
          rOut = Math.min(255, Math.floor((accR + Math.floor(accA / 2)) / accA));
          gOut = Math.min(255, Math.floor((accG + Math.floor(accA / 2)) / accA));
          bOut = Math.min(255, Math.floor((accB + Math.floor(accA / 2)) / accA));
        }
      } else {
        rOut = Math.min(255, Math.floor((accR + Math.floor(cnt / 2)) / cnt));
        gOut = Math.min(255, Math.floor((accG + Math.floor(cnt / 2)) / cnt));
        bOut = Math.min(255, Math.floor((accB + Math.floor(cnt / 2)) / cnt));
      }

      const pIdx = dy * dstW + dx;
      const r565 = keepAlpha ? Math.floor((rOut * aOut + 25 * (255 - aOut) + 127) / 255) : rOut;
      const g565 = keepAlpha ? Math.floor((gOut * aOut + 32 * (255 - aOut) + 127) / 255) : gOut;
      const b565 = keepAlpha ? Math.floor((bOut * aOut + 44 * (255 - aOut) + 127) / 255) : bOut;
      rgb565Pixels[pIdx] = ((r565 & 0xf8) << 8) | ((g565 & 0xfc) << 3) | (b565 >>> 3);

      const qOff = pIdx * 4;
      outRgba[qOff] = rOut;
      outRgba[qOff + 1] = gOut;
      outRgba[qOff + 2] = bOut;
      outRgba[qOff + 3] = aOut;
    }
  }

  return {
    width: dstW,
    height: dstH,
    sourceWidth: cropW,
    sourceHeight: cropH,
    bpp,
    entryCount,
    rgb565Pixels,
    dataUrl: encodeRgbaToPngDataUrl(outRgba, dstW, dstH),
  };
}

/**
 * Exact TypeScript mirror of `load_ico_file_scaled` in `native/covers.c`:
 * Decodes genuine Windows .ico files (ICONDIR 00 00 01 00 + ICONDIRENTRY table
 * + 1/4/8bpp palette DIB + 1-bit AND mask + 16/24/32-bit DIB + PNG-compressed entries),
 * trims symmetric empty transparent outer margins, picks the best-resolution entry up to 128x128,
 * preserves exact aspect ratio via area-averaged premultiplied downsampling,
 * and outputs crisp RGBA PNG dataUrl + RGB565 array.
 */
export function decodeWindowsIcoBuffer(buf: Uint8Array): DecodedIcoResult | null {
  if (!buf || buf.length < 22) return null;

  const reserved = buf[0] | (buf[1] << 8);
  const icoType = buf[2] | (buf[3] << 8);
  const count = buf[4] | (buf[5] << 8);

  if (reserved !== 0 || icoType !== 1 || count === 0 || count > 64) {
    return null;
  }
  if (6 + count * 16 > buf.length) {
    return null;
  }

  type IcoCandidate = {
    isPng: boolean;
    bytesInRes: number;
    imageOff: number;
    pixelOffset: number;
    palOffset: number;
    clrUsed: number;
    w: number;
    h: number;
    bpp: number;
    comp: number;
    topDown: boolean;
    score: number;
  };

  let best: IcoCandidate | null = null;

  for (let i = 0; i < count; i++) {
    const eOff = 6 + i * 16;
    const entW = buf[eOff] === 0 ? 256 : buf[eOff];
    const entH = buf[eOff + 1] === 0 ? 256 : buf[eOff + 1];
    const entColors = buf[eOff + 2];
    const entBpp = buf[eOff + 6] | (buf[eOff + 7] << 8);
    const bytesInRes = readU32LE(buf, eOff + 8);
    const imageOff = readU32LE(buf, eOff + 12);

    if (imageOff < 6 + count * 16 || imageOff + 24 > buf.length) {
      continue;
    }

    const avail = buf.length - imageOff;
    const resLen = bytesInRes >= 24 && bytesInRes <= avail ? bytesInRes : avail;

    /* Embedded PNG payload (\x89PNG\r\n\x1a\n) */
    if (
      resLen >= 33 &&
      buf[imageOff] === 0x89 &&
      buf[imageOff + 1] === 0x50 &&
      buf[imageOff + 2] === 0x4e &&
      buf[imageOff + 3] === 0x47
    ) {
      let pw = entW;
      let ph = entH;
      if (
        buf[imageOff + 12] === 0x49 &&
        buf[imageOff + 13] === 0x48 &&
        buf[imageOff + 14] === 0x44 &&
        buf[imageOff + 15] === 0x52
      ) {
        const iw = readU32BE(buf, imageOff + 16);
        const ih = readU32BE(buf, imageOff + 20);
        if (iw > 0 && iw <= 1024 && ih > 0 && ih <= 1024) {
          pw = iw;
          ph = ih;
        }
      } else {
        continue;
      }
      const effBpp = entBpp >= 1 && entBpp <= 32 ? entBpp : 32;
      const minDim = Math.min(pw, ph);
      const resScore =
        minDim >= NL_ICON_MAX_DIM
          ? NL_ICON_MAX_DIM * NL_ICON_MAX_DIM - (pw > NL_ICON_MAX_DIM ? pw - NL_ICON_MAX_DIM : 0)
          : pw * ph;
      const score = resScore * 16 + effBpp * 100 + (pw === ph ? 2000 : 0);

      if (!best || score > best.score) {
        best = {
          isPng: true,
          bytesInRes: resLen,
          imageOff,
          pixelOffset: 0,
          palOffset: 0,
          clrUsed: 0,
          w: pw,
          h: ph,
          bpp: effBpp,
          comp: 0,
          topDown: true,
          score,
        };
      }
      continue;
    }

    if (resLen < 40 || imageOff + 40 > buf.length) continue;

    const biSize = readU32LE(buf, imageOff);
    const biW = readU32LE(buf, imageOff + 4) | 0;
    const biHSigned = readU32LE(buf, imageOff + 8) | 0;
    const biPlanes = buf[imageOff + 12] | (buf[imageOff + 13] << 8);
    const bpp = buf[imageOff + 14] | (buf[imageOff + 15] << 8);
    const compression = readU32LE(buf, imageOff + 16);
    let clrUsed = readU32LE(buf, imageOff + 32);

    if (biSize < 40 || biSize > 124 || biPlanes !== 1) continue;
    if (![1, 4, 8, 16, 24, 32].includes(bpp)) continue;
    if (compression !== 0 && !(compression === 3 && (bpp === 16 || bpp === 32))) continue;
    if (biW <= 0 || biW > 1024) continue;

    const topDown = biHSigned < 0;
    const absH = topDown ? -biHSigned : biHSigned;
    const icoH = entH > 0 && absH === entH ? entH : Math.floor(absH / 2);
    if (icoH <= 0 || icoH > 1024) continue;

    let palCount = 0;
    if (bpp <= 8) {
      const maxPal = 1 << bpp;
      if (clrUsed === 0 && entColors > 0 && entColors <= maxPal) {
        clrUsed = entColors;
      }
      palCount = clrUsed > 0 && clrUsed <= maxPal ? clrUsed : maxPal;
    }

    const extraMasks = compression === 3 && biSize === 40 ? 12 : 0;
    const palOffset = imageOff + biSize + extraMasks;
    const pixelOffset = palOffset + palCount * 4;
    const bitsPerRow = biW * bpp;
    const xorStride = ((bitsPerRow + 31) >>> 5) * 4;

    if (pixelOffset + icoH * xorStride > buf.length) {
      continue;
    }

    const minDim = Math.min(biW, icoH);
    const resScore =
      minDim >= NL_ICON_MAX_DIM
        ? NL_ICON_MAX_DIM * NL_ICON_MAX_DIM - (biW > NL_ICON_MAX_DIM ? biW - NL_ICON_MAX_DIM : 0)
        : biW * icoH;
    const score = resScore * 16 + bpp * 100 + (biW === icoH ? 2000 : 0);

    if (!best || score > best.score) {
      best = {
        isPng: false,
        bytesInRes: resLen,
        imageOff,
        pixelOffset,
        palOffset,
        clrUsed: palCount,
        w: biW,
        h: icoH,
        bpp,
        comp: compression,
        topDown,
        score,
      };
    }
  }

  if (!best) return null;

  if (best.isPng) {
    const pngSlice = buf.subarray(best.imageOff, best.imageOff + best.bytesInRes);
    const decodedPng = decodePngPayloadToRgba(pngSlice);
    if (!decodedPng) return null;
    return finalizeRgbaIntoIcoResult(
      decodedPng.rgba,
      decodedPng.width,
      decodedPng.height,
      best.bpp,
      count,
      false
    );
  }

  const srcW = best.w;
  const srcH = best.h;
  const bpp = best.bpp;
  const compression = best.comp;
  const topDown = best.topDown;
  const bitsPerRow = srcW * bpp;
  const xorStride = ((bitsPerRow + 31) >>> 5) * 4;
  const xorDataOffset = best.pixelOffset;
  const palOffset = best.palOffset;
  const palCount = best.clrUsed;

  const rgba = new Uint8Array(srcW * srcH * 4);
  let anyNonzeroAlpha = false;

  for (let sy = 0; sy < srcH; sy++) {
    const fileRow = topDown ? sy : srcH - 1 - sy;
    const rowOff = xorDataOffset + fileRow * xorStride;
    for (let sx = 0; sx < srcW; sx++) {
      let r = 0;
      let g = 0;
      let b = 0;
      let a = 255;

      if (bpp <= 8) {
        let idx = 0;
        if (bpp === 8) {
          idx = buf[rowOff + sx];
        } else if (bpp === 4) {
          const bv = buf[rowOff + (sx >> 1)];
          idx = sx & 1 ? bv & 0x0f : (bv >>> 4) & 0x0f;
        } else {
          const bv = buf[rowOff + (sx >> 3)];
          idx = (bv >>> (7 - (sx & 7))) & 1;
        }
        if (idx < palCount) {
          const po = palOffset + idx * 4;
          b = buf[po];
          g = buf[po + 1];
          r = buf[po + 2];
        }
      } else if (bpp === 16) {
        const pOff = rowOff + sx * 2;
        const px = buf[pOff] | (buf[pOff + 1] << 8);
        if (compression === 3) {
          const r5 = (px >>> 11) & 0x1f;
          const g6 = (px >>> 5) & 0x3f;
          const b5 = px & 0x1f;
          r = (r5 << 3) | (r5 >>> 2);
          g = (g6 << 2) | (g6 >>> 4);
          b = (b5 << 3) | (b5 >>> 2);
        } else {
          const r5 = (px >>> 10) & 0x1f;
          const g5 = (px >>> 5) & 0x1f;
          const b5 = px & 0x1f;
          r = (r5 << 3) | (r5 >>> 2);
          g = (g5 << 3) | (g5 >>> 2);
          b = (b5 << 3) | (b5 >>> 2);
        }
      } else if (bpp === 24) {
        const pOff = rowOff + sx * 3;
        b = buf[pOff];
        g = buf[pOff + 1];
        r = buf[pOff + 2];
      } else {
        const pOff = rowOff + sx * 4;
        b = buf[pOff];
        g = buf[pOff + 1];
        r = buf[pOff + 2];
        a = buf[pOff + 3];
        if (a > 0) anyNonzeroAlpha = true;
      }

      const outOff = (sy * srcW + sx) * 4;
      rgba[outOff] = r;
      rgba[outOff + 1] = g;
      rgba[outOff + 2] = b;
      rgba[outOff + 3] = a;
    }
  }

  /* Apply 1-bit AND transparency mask for indexed / 16 / 24-bit ICOs or legacy 32bpp with all-zero alpha */
  let preserveAllZeroAlpha = false;
  if (bpp < 32 || !anyNonzeroAlpha) {
    const andStride = (((srcW + 31) >>> 5) * 4);
    const andOffset = xorDataOffset + srcH * xorStride;
    if (andOffset + srcH * andStride <= buf.length) {
      let hasTrans = false;
      for (let sy = 0; sy < srcH; sy++) {
        const fileRow = topDown ? sy : srcH - 1 - sy;
        const aRowOff = andOffset + fileRow * andStride;
        for (let sx = 0; sx < srcW; sx++) {
          const bit = (buf[aRowOff + (sx >> 3)] >>> (7 - (sx & 7))) & 1;
          if (bit) hasTrans = true;
          rgba[(sy * srcW + sx) * 4 + 3] = bit ? 0 : 255;
        }
      }
      if (bpp === 32 && !anyNonzeroAlpha && !hasTrans) {
        /* Preserve explicit alpha=0 transparent 32-bit DIBs with blank AND mask */
        for (let i = 0; i < srcW * srcH; i++) {
          rgba[i * 4 + 3] = 0;
        }
        preserveAllZeroAlpha = true;
      }
    } else if (bpp === 32 && !anyNonzeroAlpha) {
      preserveAllZeroAlpha = true;
    }
  }

  return finalizeRgbaIntoIcoResult(rgba, srcW, srcH, bpp, count, preserveAllZeroAlpha);
}

export interface BuildWindowsIcoOptions {
  width?: number;
  height?: number;
  bpp?: 16 | 24 | 32;
  topColorRgb?: [number, number, number];
  bottomColorRgb?: [number, number, number];
  borderColorRgb?: [number, number, number];
  alpha?: number;
}

/**
 * Builds a genuine uncompressed Windows .ico binary buffer (ICONDIR + ICONDIRENTRY + DIB BITMAPINFOHEADER + XOR + AND mask)
 * for automated tests and sample `.ico.tns` files in `/documents/ndless/icons/`.
 */
export function buildWindowsIcoBuffer(opts: BuildWindowsIcoOptions = {}): Uint8Array {
  const w = opts.width ?? 96;
  const h = opts.height ?? 96;
  const bpp = opts.bpp ?? 32;
  const topCol = opts.topColorRgb ?? [220, 38, 38];
  const botCol = opts.bottomColorRgb ?? [30, 58, 138];
  const borderCol = opts.borderColorRgb ?? [250, 204, 21];
  const alpha = opts.alpha ?? 255;

  const bytesPerPx = bpp >> 3;
  const xorRowStride = (w * bytesPerPx + 3) & ~3;
  const andRowStride = (((w + 31) >>> 5) * 4);
  const xorSize = xorRowStride * h;
  const andSize = andRowStride * h;
  const dibSize = 40 + xorSize + andSize;
  const totalSize = 6 + 16 + dibSize;

  const buf = new Uint8Array(totalSize);
  const write16 = (off: number, val: number) => {
    buf[off] = val & 0xff;
    buf[off + 1] = (val >>> 8) & 0xff;
  };
  const write32 = (off: number, val: number) => {
    buf[off] = val & 0xff;
    buf[off + 1] = (val >>> 8) & 0xff;
    buf[off + 2] = (val >>> 16) & 0xff;
    buf[off + 3] = (val >>> 24) & 0xff;
  };

  /* ICONDIR (6 bytes) */
  write16(0, 0);
  write16(2, 1);
  write16(4, 1);

  /* ICONDIRENTRY (16 bytes) */
  buf[6] = w >= 256 ? 0 : w;
  buf[7] = h >= 256 ? 0 : h;
  buf[8] = 0;
  buf[9] = 0;
  write16(10, 1);
  write16(12, bpp);
  write32(14, dibSize);
  write32(18, 22);

  /* BITMAPINFOHEADER (40 bytes at offset 22) */
  write32(22, 40);
  write32(26, w);
  write32(30, h * 2); // doubled height in ICO DIB
  write16(34, 1);
  write16(36, bpp);
  write32(38, 0); // BI_RGB
  write32(42, xorSize + andSize);

  const xorBase = 62;
  for (let y = 0; y < h; y++) {
    const fileRow = h - 1 - y;
    const rowOff = xorBase + fileRow * xorRowStride;
    const t = h > 1 ? y / (h - 1) : 0;

    for (let x = 0; x < w; x++) {
      const isBorder = x < 3 || x >= w - 3 || y < 3 || y >= h - 3;
      const isDiagonal = Math.abs(x * h - y * w) < h * 4;
      const r = isBorder
        ? borderCol[0]
        : isDiagonal
        ? 255
        : Math.round(topCol[0] + (botCol[0] - topCol[0]) * t);
      const g = isBorder
        ? borderCol[1]
        : isDiagonal
        ? 255
        : Math.round(topCol[1] + (botCol[1] - topCol[1]) * t);
      const b = isBorder
        ? borderCol[2]
        : isDiagonal
        ? 255
        : Math.round(topCol[2] + (botCol[2] - topCol[2]) * t);

      const pOff = rowOff + x * bytesPerPx;
      if (bpp === 16) {
        const px1555 = (((r >>> 3) & 0x1f) << 10) | (((g >>> 3) & 0x1f) << 5) | ((b >>> 3) & 0x1f);
        write16(pOff, px1555);
      } else if (bpp === 24) {
        buf[pOff] = b;
        buf[pOff + 1] = g;
        buf[pOff + 2] = r;
      } else {
        buf[pOff] = b;
        buf[pOff + 1] = g;
        buf[pOff + 2] = r;
        buf[pOff + 3] = alpha;
      }
    }
  }

  return buf;
}

/**
 * Builds a genuine PNG-compressed Windows .ico buffer from raw RGBA pixels.
 */
export function buildPngIcoBuffer(rgba: Uint8Array, w: number, h: number): Uint8Array {
  const pngBytes = encodeRgbaToRawPngBytes(rgba, w, h);
  const totalSize = 22 + pngBytes.length;
  const buf = new Uint8Array(totalSize);
  buf[2] = 1;
  buf[4] = 1;
  buf[6] = w >= 256 ? 0 : w;
  buf[7] = h >= 256 ? 0 : h;
  buf[10] = 1;
  buf[12] = 32;
  buf[14] = pngBytes.length & 0xff;
  buf[15] = (pngBytes.length >>> 8) & 0xff;
  buf[16] = (pngBytes.length >>> 16) & 0xff;
  buf[17] = (pngBytes.length >>> 24) & 0xff;
  buf[18] = 22;
  buf.set(pngBytes, 22);
  return buf;
}

function buildRgbaBitmapIcoBuffer(rgba: Uint8Array, w: number, h: number): Uint8Array {
  const xorStride = w * 4;
  const andStride = (((w + 31) >>> 5) * 4);
  const dibSize = 40 + xorStride * h + andStride * h;
  const buf = new Uint8Array(22 + dibSize);
  const write16 = (off: number, val: number) => {
    buf[off] = val & 0xff;
    buf[off + 1] = (val >>> 8) & 0xff;
  };
  const write32 = (off: number, val: number) => {
    buf[off] = val & 0xff;
    buf[off + 1] = (val >>> 8) & 0xff;
    buf[off + 2] = (val >>> 16) & 0xff;
    buf[off + 3] = (val >>> 24) & 0xff;
  };
  write16(2, 1);
  write16(4, 1);
  buf[6] = w >= 256 ? 0 : w;
  buf[7] = h >= 256 ? 0 : h;
  write16(10, 1);
  write16(12, 32);
  write32(14, dibSize);
  write32(18, 22);
  write32(22, 40);
  write32(26, w);
  write32(30, h * 2);
  write16(34, 1);
  write16(36, 32);
  write32(42, xorStride * h + andStride * h);

  const xorBase = 62;
  for (let y = 0; y < h; y++) {
    const fileRow = h - 1 - y;
    const dstRow = xorBase + fileRow * xorStride;
    for (let x = 0; x < w; x++) {
      const s = (y * w + x) * 4;
      const d = dstRow + x * 4;
      buf[d] = rgba[s + 2];     // B
      buf[d + 1] = rgba[s + 1]; // G
      buf[d + 2] = rgba[s];     // R
      buf[d + 3] = rgba[s + 3]; // A
    }
  }
  return buf;
}

function paintRectRgba(
  rgba: Uint8Array,
  w: number,
  h: number,
  rx: number,
  ry: number,
  rw: number,
  rh: number,
  col: [number, number, number, number]
) {
  const x0 = Math.max(0, Math.floor(rx));
  const y0 = Math.max(0, Math.floor(ry));
  const x1 = Math.min(w, Math.ceil(rx + rw));
  const y1 = Math.min(h, Math.ceil(ry + rh));
  for (let y = y0; y < y1; y++) {
    for (let x = x0; x < x1; x++) {
      const o = (y * w + x) * 4;
      rgba[o] = col[0];
      rgba[o + 1] = col[1];
      rgba[o + 2] = col[2];
      rgba[o + 3] = col[3];
    }
  }
}

function createIconArtRgba(kind: string, size = 96): Uint8Array {
  const rgba = new Uint8Array(size * size * 4);
  const cx = (size - 1) * 0.5;
  const cy = (size - 1) * 0.5;

  const setGradBg = (top: [number, number, number], bot: [number, number, number]) => {
    for (let y = 0; y < size; y++) {
      const t = y / (size - 1);
      const r = Math.round(top[0] + (bot[0] - top[0]) * t);
      const g = Math.round(top[1] + (bot[1] - top[1]) * t);
      const b = Math.round(top[2] + (bot[2] - top[2]) * t);
      for (let x = 0; x < size; x++) {
        const o = (y * size + x) * 4;
        rgba[o] = r;
        rgba[o + 1] = g;
        rgba[o + 2] = b;
        rgba[o + 3] = 255;
      }
    }
  };

  switch (kind) {
    case 'ndoom': {
      setGradBg([153, 27, 27], [38, 8, 8]);
      paintRectRgba(rgba, size, size, 6, 8, 84, 22, [239, 68, 68, 255]);
      paintRectRgba(rgba, size, size, 12, 13, 72, 12, [254, 202, 202, 255]);
      // Stylized full-bleed visor + horns silhouette
      paintRectRgba(rgba, size, size, 8, 32, 16, 36, [220, 38, 38, 255]);
      paintRectRgba(rgba, size, size, 72, 32, 16, 36, [220, 38, 38, 255]);
      paintRectRgba(rgba, size, size, 20, 38, 56, 34, [250, 204, 21, 255]);
      paintRectRgba(rgba, size, size, 26, 48, 44, 14, [24, 24, 27, 255]);
      paintRectRgba(rgba, size, size, 32, 52, 10, 6, [74, 222, 128, 255]);
      paintRectRgba(rgba, size, size, 54, 52, 10, 6, [74, 222, 128, 255]);
      paintRectRgba(rgba, size, size, 16, 76, 64, 12, [249, 115, 22, 255]);
      break;
    }
    case 'nquake': {
      setGradBg([120, 53, 15], [28, 16, 10]);
      // Quake crescent Q emblem filling the card
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          const dx = x - cx;
          const dy = y - (cy - 4);
          const d = Math.hypot(dx, dy);
          if (d >= 22 && d <= 38 && !(dy < -16 && Math.abs(dx) < 13)) {
            const o = (y * size + x) * 4;
            rgba[o] = 245;
            rgba[o + 1] = 158;
            rgba[o + 2] = 11;
            rgba[o + 3] = 255;
          }
        }
      }
      paintRectRgba(rgba, size, size, 41, 34, 14, 52, [251, 191, 36, 255]);
      paintRectRgba(rgba, size, size, 44, 80, 8, 10, [254, 240, 138, 255]);
      break;
    }
    case 'crafti': {
      setGradBg([56, 189, 248], [14, 116, 144]);
      // Full-bleed grass block front face
      paintRectRgba(rgba, size, size, 8, 8, 80, 24, [74, 222, 128, 255]);
      paintRectRgba(rgba, size, size, 8, 32, 80, 56, [120, 72, 42, 255]);
      // Green grass overhang pixels
      paintRectRgba(rgba, size, size, 14, 32, 14, 10, [74, 222, 128, 255]);
      paintRectRgba(rgba, size, size, 40, 32, 18, 14, [34, 197, 94, 255]);
      paintRectRgba(rgba, size, size, 68, 32, 14, 8, [74, 222, 128, 255]);
      // Stone speckles
      paintRectRgba(rgba, size, size, 18, 50, 12, 10, [161, 98, 60, 255]);
      paintRectRgba(rgba, size, size, 54, 58, 14, 10, [92, 54, 32, 255]);
      paintRectRgba(rgba, size, size, 34, 68, 12, 10, [161, 98, 60, 255]);
      break;
    }
    case 'nkaruga': {
      setGradBg([15, 23, 42], [49, 46, 129]);
      // Twin-polarity delta fighter jet (white/cyan + crimson)
      for (let y = 8; y < 88; y++) {
        const span = Math.max(3, Math.floor(((y - 8) * 40) / 80));
        for (let x = Math.floor(cx - span); x <= Math.ceil(cx + span); x++) {
          if (x < 0 || x >= size) continue;
          const o = (y * size + x) * 4;
          const leftSide = x < cx;
          rgba[o] = leftSide ? 226 : 244;
          rgba[o + 1] = leftSide ? 232 : 63;
          rgba[o + 2] = leftSide ? 240 : 94;
          rgba[o + 3] = 255;
        }
      }
      paintRectRgba(rgba, size, size, 42, 22, 12, 38, [56, 189, 248, 255]);
      break;
    }
    case '2048': {
      setGradBg([245, 158, 11], [180, 83, 9]);
      // Full-bleed warm tile grid badge
      const tiles: Array<[number, number, [number, number, number]]> = [
        [6, 6, [254, 240, 138]],
        [50, 6, [251, 146, 60]],
        [6, 50, [249, 115, 22]],
        [50, 50, [234, 179, 8]],
      ];
      for (const [tx, ty, c] of tiles) {
        paintRectRgba(rgba, size, size, tx, ty, 40, 40, [c[0], c[1], c[2], 255]);
      }
      paintRectRgba(rgba, size, size, 14, 16, 24, 6, [120, 53, 15, 255]);
      paintRectRgba(rgba, size, size, 14, 30, 24, 6, [120, 53, 15, 255]);
      paintRectRgba(rgba, size, size, 58, 58, 24, 24, [255, 255, 255, 255]);
      break;
    }
    case 'pacman': {
      setGradBg([17, 24, 39], [9, 9, 24]);
      paintRectRgba(rgba, size, size, 4, 4, 88, 6, [37, 99, 235, 255]);
      paintRectRgba(rgba, size, size, 4, 86, 88, 6, [37, 99, 235, 255]);
      for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
          const dx = x - 36;
          const dy = y - cy;
          const d = Math.hypot(dx, dy);
          if (d <= 28 && !(dx > 0 && Math.abs(dy) < dx * 0.55)) {
            const o = (y * size + x) * 4;
            rgba[o] = 250;
            rgba[o + 1] = 204;
            rgba[o + 2] = 21;
            rgba[o + 3] = 255;
          }
        }
      }
      paintRectRgba(rgba, size, size, 64, 43, 9, 9, [254, 249, 195, 255]);
      paintRectRgba(rgba, size, size, 80, 43, 9, 9, [254, 249, 195, 255]);
      break;
    }
    case 'gbc': {
      setGradBg([109, 40, 217], [46, 16, 101]);
      paintRectRgba(rgba, size, size, 12, 4, 72, 88, [167, 139, 250, 255]);
      paintRectRgba(rgba, size, size, 18, 10, 60, 38, [15, 23, 42, 255]);
      paintRectRgba(rgba, size, size, 24, 15, 48, 28, [155, 188, 15, 255]);
      paintRectRgba(rgba, size, size, 30, 20, 20, 18, [48, 98, 48, 255]);
      paintRectRgba(rgba, size, size, 22, 62, 18, 6, [30, 27, 75, 255]);
      paintRectRgba(rgba, size, size, 28, 56, 6, 18, [30, 27, 75, 255]);
      paintRectRgba(rgba, size, size, 66, 58, 9, 9, [236, 72, 153, 255]);
      paintRectRgba(rgba, size, size, 54, 68, 9, 9, [236, 72, 153, 255]);
      break;
    }
    case 'gpsp': {
      setGradBg([30, 64, 175], [15, 23, 42]);
      paintRectRgba(rgba, size, size, 4, 16, 88, 64, [99, 102, 241, 255]);
      paintRectRgba(rgba, size, size, 20, 22, 56, 52, [15, 23, 42, 255]);
      paintRectRgba(rgba, size, size, 25, 27, 46, 42, [56, 189, 248, 255]);
      paintRectRgba(rgba, size, size, 32, 35, 32, 26, [250, 204, 21, 255]);
      paintRectRgba(rgba, size, size, 8, 40, 9, 16, [30, 27, 75, 255]);
      paintRectRgba(rgba, size, size, 79, 38, 9, 9, [244, 63, 94, 255]);
      paintRectRgba(rgba, size, size, 79, 52, 9, 9, [52, 211, 153, 255]);
      break;
    }
    default: {
      // nespire
      setGradBg([220, 38, 38], [69, 10, 10]);
      paintRectRgba(rgba, size, size, 6, 18, 84, 60, [226, 232, 240, 255]);
      paintRectRgba(rgba, size, size, 12, 26, 72, 44, [51, 65, 85, 255]);
      paintRectRgba(rgba, size, size, 18, 44, 18, 8, [15, 23, 42, 255]);
      paintRectRgba(rgba, size, size, 23, 39, 8, 18, [15, 23, 42, 255]);
      paintRectRgba(rgba, size, size, 58, 46, 10, 10, [239, 68, 68, 255]);
      paintRectRgba(rgba, size, size, 72, 46, 10, 10, [239, 68, 68, 255]);
      break;
    }
  }

  return rgba;
}

export function createSampleCustomIcoFiles(): VirtualFileEntry[] {
  const nDoomIco = buildWindowsIcoBuffer({
    width: 96,
    height: 96,
    bpp: 32,
    topColorRgb: [220, 38, 38],
    bottomColorRgb: [69, 10, 10],
    borderColorRgb: [251, 146, 60],
  });
  const gbcIco = buildWindowsIcoBuffer({
    width: 96,
    height: 96,
    bpp: 24,
    topColorRgb: [139, 92, 246],
    bottomColorRgb: [46, 16, 101],
    borderColorRgb: [196, 181, 253],
  });
  const c2048Ico = buildWindowsIcoBuffer({
    width: 96,
    height: 96,
    bpp: 16,
    topColorRgb: [245, 158, 11],
    bottomColorRgb: [120, 53, 15],
    borderColorRgb: [253, 224, 71],
  });

  return [
    {
      path: '/documents/ndless/icons/nDoom.ico.tns',
      sizeBytes: nDoomIco.length,
      headerBytes: nDoomIco,
    },
    {
      path: '/documents/ndless/icons/gbc4nspire.ico.tns',
      sizeBytes: gbcIco.length,
      headerBytes: gbcIco,
    },
    {
      path: '/documents/ndless/icons/2048-CX.ico.tns',
      sizeBytes: c2048Ico.length,
      headerBytes: c2048Ico,
    },
  ];
}

export function createSimulatorCustomIcoFiles(): VirtualFileEntry[] {
  const specs: Array<{ stem: string; kind: string; asPng?: boolean }> = [
    { stem: 'nDoom', kind: 'ndoom' },
    { stem: 'nQuake', kind: 'nquake', asPng: true },
    { stem: 'Crafti', kind: 'crafti' },
    { stem: 'nKaruga', kind: 'nkaruga' },
    { stem: '2048-CX', kind: '2048' },
    { stem: 'PacMan-CX', kind: 'pacman', asPng: true },
    { stem: 'gbc4nspire', kind: 'gbc' },
    { stem: 'gpSP-Nspire', kind: 'gpsp' },
    { stem: 'NESpire', kind: 'nespire' },
  ];

  return specs.map(({ stem, kind, asPng }) => {
    const rgba = createIconArtRgba(kind, 96);
    const bytes = asPng ? buildPngIcoBuffer(rgba, 96, 96) : buildRgbaBitmapIcoBuffer(rgba, 96, 96);
    return {
      path: `/documents/ndless/icons/${stem}.ico.tns`,
      sizeBytes: bytes.length,
      headerBytes: bytes,
    };
  });
}

export function createDefaultSimulatorFilesystem(): VirtualFileEntry[] {
  return [...createInitialVirtualFilesystem(), ...createSimulatorCustomIcoFiles()];
}

export function getShortcutDefs(state: LauncherState): ShortcutDef[] {
  const defs: ShortcutDef[] = [
    { keyLabel: 'FLECHES', actionLabel: 'Naviguer / Deplacer' },
    { keyLabel: 'ENTER / 5', actionLabel: 'Lancer le jeu selectionne' },
    { keyLabel: 'A-Z / 1,3', actionLabel: 'Recherche directe accueil' },
    { keyLabel: 'ESC', actionLabel: 'Retour / Quitter' },
    { keyLabel: 'TAB', actionLabel: 'Categorie TOUS/FAV/REC' },
    { keyLabel: 'VAR / *', actionLabel: 'Ajouter/Retirer favori' },
    { keyLabel: 'TRIG / =', actionLabel: 'Page prec. / suiv.' },
    { keyLabel: 'DEL', actionLabel: 'Actualiser bibliotheque' },
    { keyLabel: 'DOC', actionLabel: 'Mode Sombre/Clair/Gris' },
    { keyLabel: 'MENU', actionLabel: 'Changer couleur accent' },
    { keyLabel: 'SAVE', actionLabel: 'Ouvrir Parametres' },
    { keyLabel: '.', actionLabel: 'Selecteur rapide de profils' },
    { keyLabel: '0 / -', actionLabel: 'Page prec. / suiv.' },
    { keyLabel: 'DEL (Param.)', actionLabel: 'Eteindre la calculatrice' },
    {
      keyLabel: HOTKEY_COMBO_SHORT_TAGS[state.settings.hotkeyCombo ?? 'ctrl_menu'],
      actionLabel: state.settings.globalHotkey
        ? 'Ouvrir depuis TI-OS (ON)'
        : 'Ouvrir depuis TI-OS (OFF)',
    },
    {
      keyLabel: 'TACTILE',
      actionLabel: isMouseModeActive(state)
        ? 'Mode Souris: Depl. & clic'
        : state.settings.touchpadNav
        ? 'Clic: Lancer / Glisser: Nav'
        : 'Clic centre: Lancer jeu',
    },
    { keyLabel: 'CTRL', actionLabel: 'Maintenir: mode Souris temp.' },
  ];

  for (const g of state.games) {
    if (g.shortcutKey && isShortcutAssignable(g.shortcutKey)) {
      defs.push({
        keyLabel: `TOUCHE ${g.shortcutKey.toUpperCase()}`,
        actionLabel: `Lancer ${g.title.slice(0, 19)}`,
      });
    }
  }
  return defs;
}

export function scanVirtualFilesystem(
  files: VirtualFileEntry[],
  selfPath = '/documents/ndless/N-Launcher.tns',
  prevGames: GameEntry[] = [],
  playCounter = 2,
  prefs?: ScanPreferences
): LauncherState {
  const prevMap = new Map<string, GameEntry>();
  for (const g of prevGames) {
    prevMap.set(g.filename.toLowerCase(), g);
  }

  /* Index virtual files by lowercase path for automatic /documents/ndless/icons/<stem>.ico.tns lookup */
  const fileByPath = new Map<string, VirtualFileEntry>();
  for (const f of files) {
    fileByPath.set(f.path.toLowerCase(), f);
  }

  const games: GameEntry[] = [];
  const diagnostics: ScannedFileDiagnostic[] = [];
  let rejectedDocs = 0;
  let rejectedSys = 0;
  let rejectedDups = 0;

  let fileOrd = 0;
  for (const file of files) {
    fileOrd++;
    const filename = getBasename(file.path);
    const inspection = inspectTnsBuffer(file.headerBytes, file.sizeBytes, file.path, selfPath);

    if (inspection.format === 'TI-DOC') {
      rejectedDocs++;
      diagnostics.push({
        path: file.path,
        filename,
        sizeBytes: file.sizeBytes,
        status: 'REJECTED_TI_DOC',
        inspection,
      });
      continue;
    }

    if (inspection.format === 'SYSTEM') {
      rejectedSys++;
      diagnostics.push({
        path: file.path,
        filename,
        sizeBytes: file.sizeBytes,
        status: 'REJECTED_SYSTEM',
        inspection,
      });
      continue;
    }

    if (
      !inspection.isExecutable ||
      (inspection.format !== 'ZEHN' && inspection.format !== 'PRG' && inspection.format !== 'BFLT')
    ) {
      diagnostics.push({
        path: file.path,
        filename,
        sizeBytes: file.sizeBytes,
        status: 'REJECTED_INVALID',
        inspection,
      });
      continue;
    }

    const isDup = games.some(
      (g) =>
        g.path.toLowerCase() === file.path.toLowerCase() ||
        (g.filename.toLowerCase() === filename.toLowerCase() && g.sizeBytes === file.sizeBytes)
    );
    if (isDup) {
      rejectedDups++;
      diagnostics.push({
        path: file.path,
        filename,
        sizeBytes: file.sizeBytes,
        status: 'REJECTED_DUPLICATE',
        inspection: {
          ...inspection,
          reason: `Doublon ignoré : même binaire (${filename}, ${file.sizeBytes} octets) déjà indexé`,
        },
      });
      continue;
    }

    const title = inspection.execName || prettifyTitleFromFilename(filename);
    const author = inspection.execAuthor || 'Ndless Homebrew';
    const { sceneKind, accentHex } = classifyGameStyle(title, filename);
    const prev = prevMap.get(filename.toLowerCase());
    const gp = prefs?.gamePrefs?.[filename.toLowerCase()];

    const defaultFav =
      prev !== undefined
        ? prev.isFavorite
        : gp?.isFavorite !== undefined
        ? gp.isFavorite
        : filename.toLowerCase() === 'ndoom.tns' || filename.toLowerCase() === 'crafti.tns';
    const defaultRank =
      prev !== undefined
        ? prev.lastPlayedRank
        : gp?.lastPlayedRank !== undefined
        ? gp.lastPlayedRank
        : filename.toLowerCase() === 'ndoom.tns'
        ? 2
        : filename.toLowerCase() === 'gbc4nspire.tns'
        ? 1
        : 0;

    const launchCount = prev?.launchCount ?? gp?.launchCount ?? (defaultRank > 0 ? defaultRank * 2 : 0);
    const playTimeSec = prev?.playTimeSec ?? gp?.playTimeSec ?? (defaultRank > 0 ? defaultRank * 145 : 0);
    const lastPlayedTs =
      prev?.lastPlayedTs ??
      gp?.lastPlayedTs ??
      (defaultRank === 2 ? 1794518400 : defaultRank === 1 ? 1794432000 : 0);
    const shortcutKey =
      prev?.shortcutKey ??
      (gp?.shortcutKey && isShortcutAssignable(gp.shortcutKey) ? gp.shortcutKey.toUpperCase() : '');

    /* Automatic custom icon association mirroring native/covers.c:
     * Priority 1: /documents/ndless/icons/<stem>.ico.tns
     * Priority 2: <game_dir>/<stem>.ico.tns
     * Priority 3: /documents/ndless/covers/<stem>.ico.tns
     * (Never loads .scr.tns as a library icon — .scr.tns is strictly for the launch animation) */
    let resolvedIconSource: IconSource = prev?.iconSource ?? 'NONE';
    let resolvedIconDataUrl: string | null = prev?.capturedIconDataUrl ?? null;

    if (resolvedIconSource === 'NONE' || !resolvedIconDataUrl) {
      const stem = extractGameStem(filename).toLowerCase();
      const slashIdx = file.path.lastIndexOf('/');
      const gameDir = slashIdx > 0 ? file.path.slice(0, slashIdx).toLowerCase() : '/documents';
      const icoCandidates = [
        `/documents/ndless/icons/${stem}.ico.tns`,
        `/documents/ndless/icons/${stem}.ico`,
        `${gameDir}/${stem}.ico.tns`,
        `${gameDir}/${stem}.ico`,
        `/documents/ndless/covers/${stem}.ico.tns`,
        `/documents/ndless/covers/${stem}.ico`,
      ];

      for (const candPath of icoCandidates) {
        const icoEntry = fileByPath.get(candPath);
        if (icoEntry && icoEntry.headerBytes) {
          const decoded = decodeWindowsIcoBuffer(icoEntry.headerBytes);
          if (decoded) {
            resolvedIconSource = 'ICO';
            resolvedIconDataUrl = decoded.dataUrl;
            break;
          }
        }
      }
    }

    games.push({
      id: file.path,
      title,
      author,
      path: file.path,
      filename,
      sizeBytes: file.sizeBytes,
      binFormat: inspection.format,
      sceneKind,
      execVersion: inspection.execVersion || 1,
      relocCount: inspection.relocCount || 777,
      zehnOffset: inspection.zehnOffset ?? 0x1ec,
      usesLcdBlit: inspection.usesLcdBlit,
      supportsHww: inspection.supportsHww,
      isFavorite: defaultFav,
      lastPlayedRank: defaultRank,
      addedOrder: file.addedOrder ?? prev?.addedOrder ?? 1000 + files.length - fileOrd,
      launchCount,
      playTimeSec,
      lastPlayedTs,
      shortcutKey,
      accentHex,
      iconSource: resolvedIconSource,
      capturedIconDataUrl: resolvedIconDataUrl,
      hasScreenshot: prev?.hasScreenshot ?? false,
      screenshotDataUrl: prev?.screenshotDataUrl ?? null,
    });

    diagnostics.push({
      path: file.path,
      filename,
      sizeBytes: file.sizeBytes,
      status: 'ACCEPTED',
      inspection,
    });
  }

  games.sort((a, b) => a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' }));

  const defaultLayout: LayoutMode =
    prefs?.settings?.layout && LAYOUT_ORDER.includes(prefs.settings.layout)
      ? prefs.settings.layout
      : '3x2';
  const defaultSort: SortOrder =
    prefs?.settings?.sortOrder && SORT_ORDER_LIST.includes(prefs.settings.sortOrder)
      ? prefs.settings.sortOrder
      : 'alpha_asc';
  const defaultNdlessPersistent = prefs?.sys?.ndlessPersistent ?? true;
  const defaultAutostart = false;
  const defaultGlobalHotkey = Boolean(prefs?.settings?.globalHotkey) && (prefs?.sys?.hasNdless ?? true);
  const rawCombo = prefs?.settings?.hotkeyCombo;
  const defaultHotkeyCombo: HotkeyCombo =
    rawCombo && HOTKEY_COMBO_ORDER.includes(rawCombo) ? rawCombo : 'ctrl_menu';
  const defaultShowBattery = false;
  const initialCacheCrc = computeLibraryCacheCrc32(games);

  const resolvedTouchpadMode: TouchpadMode =
    prefs?.settings?.touchpadMode === 'mouse' ? 'mouse' : 'touchpad';
  const rawCursorStyle = clampCursorStyle(prefs?.settings?.touchpadCursorStyle);
  const resolvedCursorStyle: CursorStyle =
    rawCursorStyle === 'none' ? 'arrow' : rawCursorStyle;
  const resolvedCursorSpeed =
    typeof prefs?.settings?.cursorSpeed === 'number'
      ? clampCursorSpeed(prefs.settings.cursorSpeed)
      : NL_CURSOR_SPEED_DEFAULT;
  const resolvedCursorSize = clampCursorSize(prefs?.settings?.cursorSize);
  const resolvedTapToClick = Boolean(prefs?.settings?.tapToClick);
  const hwIsCx2 = prefs?.sys?.hwIsCx2 ?? true;

  let state: LauncherState = {
    games,
    filteredIndices: [],
    selectedPos: 0,
    currentPage: 0,
    totalPages: 1,
    activeCategory: 'ALL',
    displayMode:
      prefs?.displayMode && DISPLAY_MODE_ORDER.includes(prefs.displayMode)
        ? prefs.displayMode
        : 'dark',
    accentColor:
      prefs?.accentColor && ACCENT_COLOR_ORDER.includes(prefs.accentColor)
        ? prefs.accentColor
        : 'blue',
    settings: {
      autostart: defaultAutostart,
      globalHotkey: defaultGlobalHotkey,
      hotkeyCombo: defaultHotkeyCombo,
      layout: defaultLayout,
      scrollDir: prefs?.settings?.scrollDir === 'horizontal' ? 'horizontal' : 'vertical',
      appOpenAnim: clampAppOpenAnimMode(prefs?.settings?.appOpenAnim),
      appCloseAnim: clampAppCloseAnimMode(prefs?.settings?.appCloseAnim),
      launchAnimMode: clampLaunchAnimMode(prefs?.settings?.launchAnimMode),
      returnAnimMode: clampReturnAnimMode(prefs?.settings?.returnAnimMode),
      showNames: Boolean(prefs?.settings?.showNames),
      showShortcuts: prefs?.settings?.showShortcuts ?? true,
      showBattery: defaultShowBattery,
      profileLockEnabled: Boolean(prefs?.settings?.profileLockEnabled),
      touchpadMode: resolvedTouchpadMode,
      touchpadNav: Boolean(prefs?.settings?.touchpadNav),
      touchpadSensitivity:
        typeof prefs?.settings?.touchpadSensitivity === 'number'
          ? clampTouchpadSensitivity(prefs.settings.touchpadSensitivity)
          : NL_TP_SENS_DEFAULT,
      cursorSpeed: resolvedCursorSpeed,
      touchpadCursorShow: resolvedTouchpadMode === 'mouse',
      touchpadCursorStyle: resolvedCursorStyle,
      cursorSize: resolvedCursorSize,
      tapToClick: resolvedTapToClick,
      sortOrder: defaultSort,
    },
    sys: {
      hasNdless: prefs?.sys?.hasNdless ?? true,
      ndlessPersistent: defaultNdlessPersistent,
      ndlessRev: prefs?.sys?.ndlessRev ?? 2026,
      hwIsCx2,
      osVersion:
        prefs?.sys?.osVersion ?? (hwIsCx2 ? '6.2.0.333 (CX II-T)' : '4.5.0.1180 (CX CAS)'),
      autostartActive: false,
      autostartDiag: 'Demarrage automatique supprime',
      hotkeyActive: defaultGlobalHotkey,
      hotkeyRamActive: prefs?.sys?.hotkeyRamActive ?? defaultGlobalHotkey,
      hotkeyDiag: defaultGlobalHotkey
        ? `RAM + startup : ${HOTKEY_COMBO_LABELS[defaultHotkeyCombo]}`
        : 'Raccourci global desactive',
      cacheValid: prefs?.sys?.cacheValid ?? true,
      cacheCrc32: prefs?.sys?.cacheCrc32 ?? initialCacheCrc,
      cacheLoadedAtBoot: prefs?.sys?.cacheLoadedAtBoot ?? true,
      oomRecoveries: prefs?.sys?.oomRecoveries ?? 0,
      priorIncompleteBoot: prefs?.sys?.priorIncompleteBoot ?? false,
      lastCrashStage: prefs?.sys?.lastCrashStage ?? 0,
      lastCrashMode: prefs?.sys?.lastCrashMode ?? '',
      bootRecoveryTriggered: false,
      bootRecoveryReason: 'none',
      batteryLevel: 'UNAVAILABLE',
      tiosBatterySource: 'UNAVAILABLE',
      batteryLastRtc: 0,
      batteryReadCount: 0,
    },
    sui: {
      screen: 'LIBRARY',
      menuCursor: 0,
      menuScroll: 0,
      displayCursor: 0,
      displayScroll: 0,
      animCursor: 0,
      hotkeyCursor: 0,
      rescan: createDefaultRescanSummary(),
      tpCursor: 0,
      tpScroll: 0,
      tpTab: 0,
      tpCustomRow: 0,
      tpStyleCursor: clampCursorStyleToCard(resolvedCursorStyle),
      tpSizeCursor: cursorSizeToIndex(resolvedCursorSize),
      scGameCursor: 0,
      scGameScroll: 0,
      scPickingKey: false,
      scKeyCursor: 1,
      scConflictPrompt: false,
      scConflictKey: '',
      scConflictOwnerIdx: -1,
      scConflictConfirmYes: false,
      subCursor: 0,
      subScroll: 0,
      searchActive: false,
      searchQuery: '',
      resetConfirmYes: false,
      resetPerformed: false,
      capActive: false,
      capSemiAuto: false,
      capCancelled: false,
      capQueue: [],
      capTotal: 0,
      capPos: 0,
      capDoneCount: 0,
      capSkipCount: 0,
      iconCursor: 0,
      iconScrollRow: 0,
      iconConfirmModal: false,
      iconConfirmYes: false,
      poweroffRequested: false,
      qpOpen: false,
      qpCursor: 0,
      profMode: 'LIST',
      profCursor: 0,
      profScroll: 0,
      profDetailIdx: 0,
      profDetailCursor: 0,
      profEditName: '',
      profAvatarCat: 0,
      profAvatarCursor: 0,
      profDeleteYes: false,
      profFromQuick: false,
      diagTab: 0,
      diagCursor: 0,
      diagScroll: 0,
      diagDetailOpen: false,
      diagConfirmModal: false,
      diagConfirmYes: false,
      diagClearReq: false,
      ctrlMouseActive: false,
      mouseX: 160,
      mouseY: 120,
      hoverValid: false,
      hoverKind: 'NONE',
      hoverIndex: 0,
      hoverSub: 0,
    },
    profiles: buildInitialProfileSystem(
      prefs,
      games,
      defaultLayout,
      Boolean(prefs?.settings?.showNames),
      prefs?.settings?.showShortcuts ?? true,
      defaultSort,
      playCounter
    ),
    launchHolding: false,
    runningGameIndex: null,
    runningStartMs: null,
    lastExecStatus: 0,
    playCounter,
    navSeq: 0,
    mouseSeq: 0,
    toastMessage: null,
    toastSeq: 0,
    diag: createInitialDiagStore(prefs?.diagEntries),
    diagnostics,
    stats: {
      scannedFiles: files.length,
      acceptedGames: games.length,
      rejectedDocs,
      rejectedSys,
      rejectedDups,
    },
  };

  /* Fault isolation diagnostics for invalid binaries & corrupt custom icons during scan */
  for (const d of diagnostics) {
    if (d.status === 'REJECTED_INVALID') {
      state = reportDiagError(
        state,
        'E201',
        d.filename,
        d.path,
        'Analyse binaire .tns',
        d.inspection.reason || 'En-tete Zehn/PRG/bFLT invalide',
        'Ce fichier est isole et exclu de la grille',
        'Autres jeux conserves et lancables'
      );
    }
  }
  for (const file of files) {
    const lower = file.path.toLowerCase();
    if ((lower.endsWith('.ico.tns') || lower.endsWith('.ico')) && file.headerBytes) {
      if (!decodeWindowsIcoBuffer(file.headerBytes)) {
        state = reportDiagError(
          state,
          'E103',
          getBasename(file.path),
          file.path,
          'Decodage icone ICO',
          'En-tete ou masque ICO invalide',
          'Jeu conserve avec icone par defaut',
          'Icone de remplacement utilisee'
        );
      }
    }
  }

  return applyActiveProfile(state);
}

/* =========================================================================
 * #R9 Centralized Diagnostic System, Boot Forensics & Fault Isolation
 * ========================================================================= */

const BOOT_STAGE_LABELS: readonly string[] = [
  '0. Inconnu',
  '1. Entree N Launcher',
  '2. Args & dossiers',
  '3. Init memoire & diag',
  '4. Detection OS / Ndless',
  '5. Chargement config',
  '6. Verification cache',
  '7. Init profils',
  '8. Scan bibliotheque',
  '9. Chargement icones',
  '10. Init ecran & theme',
  '11. Pret boucle UI',
  '12. Sortie propre',
];

export function getBootStageLabel(stage: number): string {
  if (stage >= 1 && stage <= NL_BOOT_STAGE_COUNT) {
    return BOOT_STAGE_LABELS[stage];
  }
  return BOOT_STAGE_LABELS[0];
}

interface DiagDefaultTemplate {
  category: string;
  title: string;
  reason: string;
  impact: string;
  recovery: string;
  resolved: boolean;
}

const DIAG_DEFAULT_MAP: Record<DiagCode, DiagDefaultTemplate> = {
  E102: {
    category: 'Icone',
    title: 'Icone introuvable',
    reason: 'Fichier icone absent sur le stockage',
    impact: 'Le jeu reste disponible et lancable',
    recovery: 'Icone de remplacement par defaut',
    resolved: true,
  },
  E103: {
    category: 'Icone',
    title: 'Icone corrompue',
    reason: 'En-tete ou pixels icone invalides',
    impact: 'Icone defectueuse ignoree sans crash',
    recovery: 'Icone de remplacement affichee',
    resolved: true,
  },
  E104: {
    category: 'Capture',
    title: 'Capture ecran absente',
    reason: 'Fichier .scr.tns manquant',
    impact: 'Aucun blocage au lancement du jeu',
    recovery: 'Animation de zoom de remplacement',
    resolved: true,
  },
  E201: {
    category: 'Jeu',
    title: 'Jeu mal configure / invalide',
    reason: 'En-tete Zehn/PRG/bFLT invalide ou illisible',
    impact: 'Ce fichier est isole et exclu seul',
    recovery: 'Autres jeux conserves et disponibles',
    resolved: true,
  },
  E301: {
    category: 'Configuration',
    title: 'Configuration illisible',
    reason: 'Fichier nlauncher.cfg.tns corrompu',
    impact: 'Reglages par defaut appliques',
    recovery: 'Configuration sure chargee',
    resolved: true,
  },
  E401: {
    category: 'Dossier',
    title: 'Dossier systeme manquant',
    reason: 'Repertoire requis introuvable',
    impact: 'Stockage secondaire incomplet',
    recovery: 'Dossier recree sans perte de donnees',
    resolved: true,
  },
  E501: {
    category: 'Graphique',
    title: 'Ressource graphique invalide',
    reason: 'Tampon ou sprite graphique invalide',
    impact: 'Ressource defectueuse ignoree',
    recovery: 'Interface graphique maintenue',
    resolved: true,
  },
  E601: {
    category: 'Profil',
    title: 'Fichier de profil corrompu',
    reason: 'Checksum ou structure profil invalide',
    impact: 'Original preserve (.bak.tns)',
    recovery: 'Chargement de recuperation tente',
    resolved: true,
  },
  E801: {
    category: 'Memoire',
    title: 'Memoire insuffisante',
    reason: 'Echec allocation tampon VRAM ou image',
    impact: 'Effets lourds ou captures reduits',
    recovery: 'Tampons non critiques liberes',
    resolved: true,
  },
  E901: {
    category: 'Demarrage',
    title: 'Arret anormal detecte',
    reason: 'Session precedente interrompue avant fin',
    impact: 'Derniere etape tracee au boot',
    recovery: 'Journaux et donnees preserves',
    resolved: true,
  },
};

export function getDiagDefaults(code: DiagCode): DiagDefaultTemplate {
  return DIAG_DEFAULT_MAP[code];
}

function estimateDiagLogBytes(entries: DiagEntry[]): number {
  return entries.reduce((acc, e) => acc + 180 + e.path.length + e.reason.length, 0);
}

export function createInitialDiagStore(seedEntries?: DiagEntry[]): DiagStore {
  const stageReached = Array.from({ length: NL_BOOT_STAGE_COUNT + 1 }, (_, i) => i >= 1 && i <= 11);
  const entries = Array.isArray(seedEntries) ? seedEntries.slice(0, NL_DIAG_MAX_ENTRIES) : [];
  return {
    entries,
    currentStage: 11,
    stageReached,
    warningSummaryShown: false,
    logBytes: estimateDiagLogBytes(entries),
  };
}

export function recordBootStage(
  state: LauncherState,
  stage: number,
  cleanExit = false
): LauncherState {
  if (stage < 1 || stage > NL_BOOT_STAGE_COUNT) return state;
  const stageReached = [...state.diag.stageReached];
  stageReached[stage] = true;
  return {
    ...state,
    diag: {
      ...state.diag,
      currentStage: stage,
      stageReached,
    },
    sys: cleanExit
      ? {
          ...state.sys,
          priorIncompleteBoot: false,
        }
      : state.sys,
  };
}

export function reportDiagError(
  state: LauncherState,
  code: DiagCode,
  target: string,
  path: string,
  operation: string,
  reason?: string,
  impact?: string,
  recovery?: string
): LauncherState {
  const tpl = getDiagDefaults(code);
  const normTarget = target || path || 'N-Launcher';
  const normPath = path || '/documents/ndless/';
  const existingIdx = state.diag.entries.findIndex(
    (e) => e.code === code && e.path.toLowerCase() === normPath.toLowerCase()
  );

  let nextEntries: DiagEntry[];
  if (existingIdx >= 0) {
    nextEntries = state.diag.entries.map((e, i) =>
      i === existingIdx ? { ...e, count: e.count + 1 } : e
    );
  } else {
    const newEntry: DiagEntry = {
      code,
      stage: state.diag.currentStage || 8,
      count: 1,
      resolved: tpl.resolved,
      category: tpl.category,
      title: tpl.title,
      target: normTarget,
      path: normPath,
      operation: operation || 'Operation launcher',
      reason: reason || tpl.reason,
      impact: impact || tpl.impact,
      recovery: recovery || tpl.recovery,
    };
    if (state.diag.entries.length >= NL_DIAG_MAX_ENTRIES) {
      nextEntries = [...state.diag.entries.slice(1), newEntry];
    } else {
      nextEntries = [...state.diag.entries, newEntry];
    }
  }

  return {
    ...state,
    diag: {
      ...state.diag,
      entries: nextEntries,
      logBytes: estimateDiagLogBytes(nextEntries),
    },
  };
}

export function clearDiagLogs(state: LauncherState): LauncherState {
  /* Safe log clearing: preserves games, saves, profiles, icons, settings, and active stage markers */
  return {
    ...state,
    diag: {
      ...state.diag,
      entries: [],
      warningSummaryShown: false,
      logBytes: 0,
    },
    sys: {
      ...state.sys,
      priorIncompleteBoot: false,
      lastCrashStage: 0,
      lastCrashMode: '',
    },
    sui: {
      ...state.sui,
      diagCursor: 0,
      diagScroll: 0,
      diagDetailOpen: false,
      diagConfirmModal: false,
      diagConfirmYes: false,
      diagClearReq: true,
    },
    toastMessage: 'Journaux diagnostiques effaces',
    toastSeq: state.toastSeq + 1,
  };
}

export function recoverMemoryOnOom(
  state: LauncherState,
  context: string,
  path = '/documents/ndless/N-Launcher.tns'
): LauncherState {
  const freedGames = state.games.map((g) => ({
    ...g,
    screenshotDataUrl: null,
  }));
  const updated: LauncherState = {
    ...state,
    games: freedGames,
    sys: {
      ...state.sys,
      oomRecoveries: state.sys.oomRecoveries + 1,
    },
  };
  return reportDiagError(
    updated,
    'E801',
    context || 'Allocation memoire',
    path,
    context || 'Allocation tampon',
    'Memoire libre insuffisante (CX II-T heap)',
    'Captures non critiques liberees proprement',
    'Interface maintenue sans plantage'
  );
}

export function setHardwarePreset(
  state: LauncherState,
  preset: HardwarePresetId
): LauncherState {
  const isCx2 = preset === 'CX2T_620';
  return {
    ...state,
    sys: {
      ...state.sys,
      hwIsCx2: isCx2,
      osVersion: isCx2 ? '6.2.0.333 (CX II-T)' : '4.5.0.1180 (CX CAS)',
      ndlessRev: isCx2 ? 2026 : 2005,
    },
    toastMessage: isCx2 ? 'Cible : CX II-T (OS 6.2.0)' : 'Cible : CX CAS (OS 4.5.0)',
    toastSeq: state.toastSeq + 1,
  };
}

export function simulateFaultInjection(
  state: LauncherState,
  kind:
    | 'MISSING_ICON'
    | 'CORRUPT_ICON'
    | 'MISSING_SCREENSHOT'
    | 'CORRUPT_GAME'
    | 'CORRUPT_CONFIG'
    | 'MISSING_DIR'
    | 'CORRUPT_PROFILE'
    | 'OOM'
    | 'PREV_CRASH'
): LauncherState {
  const g0 = state.games[0];
  const gStem = g0 ? extractGameStem(g0.filename).toLowerCase() : 'ndoom';
  const gTitle = g0?.title || 'Minecraft';
  switch (kind) {
    case 'MISSING_ICON':
      return {
        ...reportDiagError(
          state,
          'E102',
          gTitle,
          `/documents/ndless/icons/${gStem}.ico.tns`,
          'Chargement icone',
          'Fichier icone introuvable',
          'Le jeu reste disponible et lancable',
          'Icone de remplacement utilisee'
        ),
        toastMessage: `E102 (${gTitle}) : icone de remplacement`,
        toastSeq: state.toastSeq + 1,
      };
    case 'CORRUPT_ICON':
      return {
        ...reportDiagError(
          state,
          'E103',
          gTitle,
          `/documents/ndless/icons/${gStem}.ico.tns`,
          'Decodage icone ICO',
          'Donnees BMP/ICO corrompues',
          'Jeu conserve sans icone perso',
          'Icone par defaut affichee'
        ),
        toastMessage: `E103 (${gTitle}) : icone isolee`,
        toastSeq: state.toastSeq + 1,
      };
    case 'MISSING_SCREENSHOT':
      return {
        ...reportDiagError(
          state,
          'E104',
          gTitle,
          `/documents/ndless/covers/${gStem}.scr.tns`,
          'Chargement capture',
          'Fichier capture .scr.tns absent',
          'Le jeu se lance normalement',
          'Animation de remplacement utilisee'
        ),
        toastMessage: `E104 (${gTitle}) : animation de secours`,
        toastSeq: state.toastSeq + 1,
      };
    case 'CORRUPT_GAME':
      return {
        ...reportDiagError(
          state,
          'E201',
          'corrupted_rom.tns',
          '/documents/ndless/games/corrupted_rom.tns',
          'Verification Zehn/PRG',
          'Signature binaire invalide (00 00 FF FF)',
          'Ce jeu seul est exclu',
          'Bibliotheque intacte pour les autres jeux'
        ),
        toastMessage: 'E201 : jeu corrompu isole sans crash',
        toastSeq: state.toastSeq + 1,
      };
    case 'CORRUPT_CONFIG': {
      const cfgErr = reportDiagError(
        state,
        'E301',
        'nlauncher.cfg.tns',
        '/documents/ndless/nlauncher.cfg.tns',
        'Lecture configuration',
        'Config illisible / tronquee',
        'Reglages par defaut charges',
        'Original preserve (.bak.tns)'
      );
      return {
        ...cfgErr,
        toastMessage: 'E301 : config par defaut chargee',
        toastSeq: state.toastSeq + 1,
      };
    }
    case 'MISSING_DIR':
      return {
        ...reportDiagError(
          state,
          'E401',
          'covers',
          '/documents/ndless/covers',
          'Verification dossiers',
          'Dossier covers absent',
          'Recree automatiquement',
          'Aucune perte de donnees'
        ),
        toastMessage: 'E401 : dossier recree sans risque',
        toastSeq: state.toastSeq + 1,
      };
    case 'CORRUPT_PROFILE':
      return {
        ...reportDiagError(
          state,
          'E601',
          'nlauncher.profiles.tns',
          '/documents/ndless/nlauncher.profiles.tns',
          'Chargement profils',
          'Header/CRC profils invalide',
          'Original preserve en .bak.tns',
          'Recuperation + Profil 1 par defaut'
        ),
        toastMessage: 'E601 : profils preserves (.bak.tns)',
        toastSeq: state.toastSeq + 1,
      };
    case 'OOM':
      return {
        ...recoverMemoryOnOom(state, 'Buffer framebuffer/icones', '/documents/ndless/N-Launcher.tns'),
        toastMessage: 'E801 : memoire liberee proprement',
        toastSeq: state.toastSeq + 1,
      };
    case 'PREV_CRASH':
      return {
        ...reportDiagError(
          {
            ...state,
            sys: {
              ...state.sys,
              priorIncompleteBoot: true,
              lastCrashStage: 3,
              lastCrashMode: state.sys.ndlessPersistent ? 'Ndless Persistant' : 'Ndless Classique',
            },
          },
          'E901',
          'nlauncher.stage.tns',
          '/documents/ndless/nlauncher.stage.tns',
          'Reprise apres arret',
          'Arret a: 3. Init memoire & diag',
          `Mode: ${state.sys.ndlessPersistent ? 'Persistant' : 'Classique'} | OS: ${state.sys.osVersion}`,
          'Trace des 12 etapes preservee'
        ),
        toastMessage: 'E901 : trace de boot anterieur chargee',
        toastSeq: state.toastSeq + 1,
      };
  }
}

/* =========================================================================
 * Multi-User Profile Management & Per-Profile Isolation Helpers
 * ========================================================================= */

function sanitizeProfileName(raw: string, fallbackNum: number): string {
  const cleaned = raw
    .replace(/[^a-zA-Z0-9 _-]/g, '')
    .trim()
    .slice(0, NL_PROFILE_NAME_MAX)
    .trim();
  return cleaned || `Profil ${fallbackNum}`;
}

function buildInitialProfileSystem(
  prefs: ScanPreferences | undefined,
  games: GameEntry[],
  defaultLayout: LayoutMode,
  defaultShowNames: boolean,
  defaultShowShortcuts: boolean,
  defaultSort: SortOrder,
  playCounter: number
): ProfileSystemState {
  const saved = prefs?.profiles;
  if (saved && Array.isArray(saved.items) && saved.items.length > 0) {
    const used = new Set<number>();
    let hasPrimary = false;
    let maxId = 1;
    const sanitized: UserProfile[] = [];

    for (let i = 0; i < saved.items.length && i < NL_MAX_PROFILES; i++) {
      const src = saved.items[i];
      if (!src) continue;
      const id = typeof src.id === 'number' && src.id > 0 ? Math.floor(src.id) : i + 1;
      if (id > maxId) maxId = id;

      let avatarId =
        typeof src.avatarId === 'number' &&
        src.avatarId >= 0 &&
        src.avatarId < NL_AVATAR_COUNT
          ? Math.floor(src.avatarId)
          : 0;
      if (used.has(avatarId)) {
        let alt = 0;
        while (alt < NL_AVATAR_COUNT && used.has(alt)) alt++;
        avatarId = alt < NL_AVATAR_COUNT ? alt : 0;
      }
      used.add(avatarId);

      const isPrimary = Boolean(src.isPrimary) && !hasPrimary;
      if (isPrimary) hasPrimary = true;

      const cleanGameData: Record<string, ProfileGameData> = {};
      if (src.gameData && typeof src.gameData === 'object') {
        for (const [k, v] of Object.entries(src.gameData)) {
          if (!v) continue;
          const sc =
            typeof v.shortcutKey === 'string' && isShortcutAssignable(v.shortcutKey)
              ? v.shortcutKey.toUpperCase()
              : '';
          cleanGameData[k.toLowerCase()] = {
            isFavorite: Boolean(v.isFavorite),
            lastPlayedRank: Math.max(0, Math.floor(v.lastPlayedRank || 0)),
            launchCount: Math.max(0, Math.floor(v.launchCount || 0)),
            playTimeSec: Math.max(0, Math.floor(v.playTimeSec || 0)),
            lastPlayedTs: Math.max(0, Math.floor(v.lastPlayedTs || 0)),
            shortcutKey: sc,
          };
        }
      }

      sanitized.push({
        id,
        name: sanitizeProfileName(src.name || '', i + 1),
        avatarId,
        isPrimary,
        isLocked: Boolean(src.isLocked),
        layout: src.layout && LAYOUT_ORDER.includes(src.layout) ? src.layout : defaultLayout,
        scrollDir: src.scrollDir === 'horizontal' ? 'horizontal' : 'vertical',
        appOpenAnim: clampAppOpenAnimMode(src.appOpenAnim ?? prefs?.settings?.appOpenAnim),
        appCloseAnim: clampAppCloseAnimMode(src.appCloseAnim ?? prefs?.settings?.appCloseAnim),
        launchAnimMode: clampLaunchAnimMode(src.launchAnimMode ?? prefs?.settings?.launchAnimMode),
        returnAnimMode: clampReturnAnimMode(src.returnAnimMode ?? prefs?.settings?.returnAnimMode),
        showNames: typeof src.showNames === 'boolean' ? src.showNames : defaultShowNames,
        showShortcuts:
          typeof src.showShortcuts === 'boolean' ? src.showShortcuts : defaultShowShortcuts,
        sortOrder:
          src.sortOrder && SORT_ORDER_LIST.includes(src.sortOrder)
            ? src.sortOrder
            : defaultSort,
        activeCategory:
          src.activeCategory && CATEGORY_ORDER.includes(src.activeCategory)
            ? src.activeCategory
            : 'ALL',
        selectedPos: Math.max(0, Math.floor(src.selectedPos || 0)),
        playCounter: Math.max(0, Math.floor(src.playCounter || 0)),
        gameData: cleanGameData,
      });
    }

    if (sanitized.length > 0) {
      if (!hasPrimary) sanitized[0].isPrimary = true;
      const activeIdx =
        typeof saved.activeIndex === 'number' &&
        saved.activeIndex >= 0 &&
        saved.activeIndex < sanitized.length
          ? Math.floor(saved.activeIndex)
          : 0;
      const nextProfileId = Math.max(
        maxId + 1,
        typeof saved.nextProfileId === 'number' ? Math.floor(saved.nextProfileId) : maxId + 1
      );
      return {
        items: sanitized,
        activeIndex: activeIdx,
        nextProfileId,
      };
    }
  }

  /* Legacy single-user migration into Primary "Profil 1" (avatar 0) */
  const gameData: Record<string, ProfileGameData> = {};
  for (const g of games) {
    if (
      g.isFavorite ||
      g.lastPlayedRank > 0 ||
      g.launchCount > 0 ||
      g.playTimeSec > 0 ||
      g.lastPlayedTs > 0 ||
      g.shortcutKey
    ) {
      gameData[g.filename.toLowerCase()] = {
        isFavorite: g.isFavorite,
        lastPlayedRank: g.lastPlayedRank,
        launchCount: g.launchCount,
        playTimeSec: g.playTimeSec,
        lastPlayedTs: g.lastPlayedTs,
        shortcutKey: g.shortcutKey || '',
      };
    }
  }

  return {
    items: [
      {
        id: 1,
        name: 'Profil 1',
        avatarId: 0,
        isPrimary: true,
        isLocked: false,
        layout: defaultLayout,
        scrollDir: prefs?.settings?.scrollDir === 'horizontal' ? 'horizontal' : 'vertical',
        appOpenAnim: clampAppOpenAnimMode(prefs?.settings?.appOpenAnim),
        appCloseAnim: clampAppCloseAnimMode(prefs?.settings?.appCloseAnim),
        launchAnimMode: clampLaunchAnimMode(prefs?.settings?.launchAnimMode),
        returnAnimMode: clampReturnAnimMode(prefs?.settings?.returnAnimMode),
        showNames: defaultShowNames,
        showShortcuts: defaultShowShortcuts,
        sortOrder: defaultSort,
        activeCategory: 'ALL',
        selectedPos: 0,
        playCounter,
        gameData,
      },
    ],
    activeIndex: 0,
    nextProfileId: 2,
  };
}

export function findAvatarOwner(
  state: LauncherState,
  avatarId: number,
  excludeProfileIdx = -1
): number {
  if (avatarId < 0 || avatarId >= NL_AVATAR_COUNT) return -1;
  for (let i = 0; i < state.profiles.items.length; i++) {
    if (i === excludeProfileIdx) continue;
    if (state.profiles.items[i].avatarId === avatarId) return i;
  }
  return -1;
}

export function firstFreeAvatar(state: LauncherState): number {
  for (let id = 0; id < NL_AVATAR_COUNT; id++) {
    if (findAvatarOwner(state, id, -1) < 0) return id;
  }
  return -1;
}

export function getActiveProfile(state: LauncherState): UserProfile {
  const items = state.profiles.items;
  const idx =
    state.profiles.activeIndex >= 0 && state.profiles.activeIndex < items.length
      ? state.profiles.activeIndex
      : 0;
  return items[idx];
}

export function snapshotActiveProfile(state: LauncherState): LauncherState {
  if (state.profiles.items.length === 0) return state;
  const idx =
    state.profiles.activeIndex >= 0 &&
    state.profiles.activeIndex < state.profiles.items.length
      ? state.profiles.activeIndex
      : 0;

  const prevProf = state.profiles.items[idx];
  const nextGameData: Record<string, ProfileGameData> = { ...prevProf.gameData };

  for (const g of state.games) {
    const key = g.filename.toLowerCase();
    const hasData =
      g.isFavorite ||
      g.lastPlayedRank > 0 ||
      g.launchCount > 0 ||
      g.playTimeSec > 0 ||
      g.lastPlayedTs > 0 ||
      Boolean(g.shortcutKey);
    if (hasData || nextGameData[key]) {
      nextGameData[key] = {
        isFavorite: g.isFavorite,
        lastPlayedRank: g.lastPlayedRank,
        launchCount: g.launchCount,
        playTimeSec: g.playTimeSec,
        lastPlayedTs: g.lastPlayedTs,
        shortcutKey: g.shortcutKey || '',
      };
    }
  }

  const updatedProfile: UserProfile = {
    ...prevProf,
    layout: state.settings.layout,
    scrollDir: state.settings.scrollDir ?? 'vertical',
    appOpenAnim: clampAppOpenAnimMode(state.settings.appOpenAnim),
    appCloseAnim: clampAppCloseAnimMode(state.settings.appCloseAnim),
    launchAnimMode: clampLaunchAnimMode(state.settings.launchAnimMode),
    returnAnimMode: clampReturnAnimMode(state.settings.returnAnimMode),
    showNames: state.settings.showNames,
    showShortcuts: state.settings.showShortcuts,
    sortOrder: state.settings.sortOrder,
    activeCategory: state.activeCategory,
    selectedPos: state.selectedPos,
    playCounter: state.playCounter,
    gameData: nextGameData,
  };

  const nextItems = state.profiles.items.map((p, i) => (i === idx ? updatedProfile : p));
  return {
    ...state,
    profiles: {
      ...state.profiles,
      items: nextItems,
      activeIndex: idx,
    },
  };
}

export function applyActiveProfile(state: LauncherState): LauncherState {
  if (state.profiles.items.length === 0) return rebuildFilter(state);
  const idx =
    state.profiles.activeIndex >= 0 &&
    state.profiles.activeIndex < state.profiles.items.length
      ? state.profiles.activeIndex
      : 0;
  const prof = state.profiles.items[idx];

  const usedKeys = new Set<string>();
  const nextGames = state.games.map((g) => {
    const rec = prof.gameData[g.filename.toLowerCase()];
    let sc = '';
    if (rec?.shortcutKey && isShortcutAssignable(rec.shortcutKey)) {
      const up = rec.shortcutKey.toUpperCase();
      if (!usedKeys.has(up)) {
        usedKeys.add(up);
        sc = up;
      }
    }
    return {
      ...g,
      isFavorite: Boolean(rec?.isFavorite),
      lastPlayedRank: rec?.lastPlayedRank ?? 0,
      launchCount: rec?.launchCount ?? 0,
      playTimeSec: rec?.playTimeSec ?? 0,
      lastPlayedTs: rec?.lastPlayedTs ?? 0,
      shortcutKey: sc,
    };
  });

  return rebuildFilter({
    ...state,
    games: nextGames,
    settings: {
      ...state.settings,
      layout: prof.layout,
      scrollDir: prof.scrollDir ?? state.settings.scrollDir ?? 'vertical',
      appOpenAnim: clampAppOpenAnimMode(prof.appOpenAnim ?? state.settings.appOpenAnim),
      appCloseAnim: clampAppCloseAnimMode(prof.appCloseAnim ?? state.settings.appCloseAnim),
      launchAnimMode: clampLaunchAnimMode(prof.launchAnimMode ?? state.settings.launchAnimMode),
      returnAnimMode: clampReturnAnimMode(prof.returnAnimMode ?? state.settings.returnAnimMode),
      showNames: prof.showNames,
      showShortcuts: prof.showShortcuts,
      sortOrder: prof.sortOrder,
    },
    activeCategory: prof.activeCategory,
    selectedPos: prof.selectedPos,
    playCounter: prof.playCounter,
    sui: {
      ...state.sui,
      searchActive: false,
      searchQuery: '',
    },
    profiles: {
      ...state.profiles,
      activeIndex: idx,
    },
  });
}

export function computeQuickProfileSlotCenterX(profileCount: number, slot: number): number {
  const pcount = Math.max(1, profileCount);
  const totalSlots = pcount + 1;
  const visMax = 6;
  const diam = 34;
  const stride = 44;
  const clampedSlot = Math.max(0, Math.min(totalSlots - 1, slot));
  const shown = Math.min(totalSlots, visMax);
  let winStart = 0;
  if (totalSlots > visMax) {
    winStart = clampedSlot - Math.floor(visMax / 2);
    if (winStart < 0) winStart = 0;
    if (winStart > totalSlots - visMax) winStart = totalSlots - visMax;
  }
  const stripW = shown * diam + (shown - 1) * (stride - diam);
  const startX = 160 - Math.floor(stripW / 2);
  const rel = clampedSlot - winStart;
  return startX + rel * stride + Math.floor(diam / 2);
}

export function switchProfileTo(
  state: LauncherState,
  targetIdx: number,
  originX?: number,
  originY?: number,
  originD?: number
): LauncherState {
  if (targetIdx < 0 || targetIdx >= state.profiles.items.length) return state;
  if (targetIdx === state.profiles.activeIndex) return state;
  const pcount = state.profiles.items.length;
  const defaultOrigin =
    state.sui.screen === 'LIBRARY' && state.sui.qpOpen
      ? { x: computeQuickProfileSlotCenterX(pcount, targetIdx), y: 114, d: 36 }
      : state.sui.screen === 'LIBRARY'
      ? { x: 17, y: 12, d: 16 }
      : { x: 48, y: 86, d: 40 };

  const snapped = snapshotActiveProfile(state);
  const applied = applyActiveProfile({
    ...snapped,
    profiles: {
      ...snapped.profiles,
      activeIndex: targetIdx,
    },
  });
  const prevAvatarId = state.profiles.items[state.profiles.activeIndex]?.avatarId ?? 0;
  const prof = applied.profiles.items[targetIdx];
  return {
    ...applied,
    navSeq: applied.navSeq + 1,
    profSwitchSeq: (state.profSwitchSeq ?? 0) + 1,
    profSwitchFromAvatarId: prevAvatarId,
    profSwitchAvatarId: prof.avatarId,
    profSwitchOriginX: originX ?? defaultOrigin.x,
    profSwitchOriginY: originY ?? defaultOrigin.y,
    profSwitchOriginD: originD ?? defaultOrigin.d,
    toastMessage: `Profil : ${prof.name}`,
    toastSeq: applied.toastSeq + 1,
  };
}

export function stepProfile(state: LauncherState, delta: number): LauncherState {
  const next = state.profiles.activeIndex + delta;
  /* Strict non-wrapping boundary at index 0 and last profile */
  if (
    next < 0 ||
    next >= state.profiles.items.length ||
    next === state.profiles.activeIndex
  ) {
    return state;
  }
  const isQuick = state.sui.screen === 'LIBRARY' && state.sui.qpOpen;
  return switchProfileTo(
    state,
    next,
    isQuick ? computeQuickProfileSlotCenterX(state.profiles.items.length, next) : 17,
    isQuick ? 114 : 12,
    isQuick ? 36 : 16
  );
}

export function createUserProfile(
  state: LauncherState,
  name: string,
  avatarId: number
): { state: LauncherState; createdIdx: number } {
  if (state.profiles.items.length >= NL_MAX_PROFILES) {
    return { state, createdIdx: -1 };
  }
  if (avatarId < 0 || avatarId >= NL_AVATAR_COUNT || findAvatarOwner(state, avatarId, -1) >= 0) {
    return { state, createdIdx: -1 };
  }
  const snapped = snapshotActiveProfile(state);
  const newIdx = snapped.profiles.items.length;
  const newProf: UserProfile = {
    id: snapped.profiles.nextProfileId,
    name: sanitizeProfileName(name, newIdx + 1),
    avatarId,
    isPrimary: false,
    isLocked: false,
    layout: '3x2',
    scrollDir: snapped.settings.scrollDir ?? 'vertical',
    appOpenAnim: clampAppOpenAnimMode(snapped.settings.appOpenAnim),
    appCloseAnim: clampAppCloseAnimMode(snapped.settings.appCloseAnim),
    launchAnimMode: clampLaunchAnimMode(snapped.settings.launchAnimMode),
    returnAnimMode: clampReturnAnimMode(snapped.settings.returnAnimMode),
    showNames: false,
    showShortcuts: false,
    sortOrder: 'alpha_asc',
    activeCategory: 'ALL',
    selectedPos: 0,
    playCounter: 0,
    gameData: {},
  };

  const withNew = applyActiveProfile({
    ...snapped,
    profiles: {
      items: [...snapped.profiles.items, newProf],
      activeIndex: newIdx,
      nextProfileId: snapped.profiles.nextProfileId + 1,
    },
  });

  return {
    state: {
      ...withNew,
      navSeq: withNew.navSeq + 1,
    },
    createdIdx: newIdx,
  };
}

export function isProfileProtected(state: LauncherState, profIdx: number): boolean {
  if (!state.settings.profileLockEnabled) return false;
  if (profIdx < 0 || profIdx >= state.profiles.items.length) return false;
  return Boolean(state.profiles.items[profIdx].isLocked);
}

export function toggleProfileLock(
  state: LauncherState,
  profIdx: number
): { state: LauncherState; ok: boolean; isLocked: boolean } {
  if (profIdx < 0 || profIdx >= state.profiles.items.length) {
    return { state, ok: false, isLocked: false };
  }
  const nextLocked = !Boolean(state.profiles.items[profIdx].isLocked);
  return {
    state: {
      ...state,
      profiles: {
        ...state.profiles,
        items: state.profiles.items.map((p, i) =>
          i === profIdx ? { ...p, isLocked: nextLocked } : p
        ),
      },
      toastMessage: nextLocked ? 'Profil verrouille' : 'Profil deverrouille',
      toastSeq: state.toastSeq + 1,
    },
    ok: true,
    isLocked: nextLocked,
  };
}

export function renameUserProfile(
  state: LauncherState,
  profIdx: number,
  newName: string
): { state: LauncherState; ok: boolean } {
  if (profIdx < 0 || profIdx >= state.profiles.items.length) {
    return { state, ok: false };
  }
  if (isProfileProtected(state, profIdx)) {
    return { state, ok: false };
  }
  const cleaned = newName
    .replace(/[^a-zA-Z0-9 _-]/g, '')
    .trim()
    .slice(0, NL_PROFILE_NAME_MAX)
    .trim();
  if (!cleaned) return { state, ok: false };

  return {
    state: {
      ...state,
      profiles: {
        ...state.profiles,
        items: state.profiles.items.map((p, i) => (i === profIdx ? { ...p, name: cleaned } : p)),
      },
    },
    ok: true,
  };
}

export function setUserProfileAvatar(
  state: LauncherState,
  profIdx: number,
  avatarId: number
): { state: LauncherState; ok: boolean } {
  if (profIdx < 0 || profIdx >= state.profiles.items.length) return { state, ok: false };
  if (isProfileProtected(state, profIdx)) return { state, ok: false };
  if (avatarId < 0 || avatarId >= NL_AVATAR_COUNT) return { state, ok: false };
  if (findAvatarOwner(state, avatarId, profIdx) >= 0) return { state, ok: false };

  return {
    state: {
      ...state,
      profiles: {
        ...state.profiles,
        items: state.profiles.items.map((p, i) => (i === profIdx ? { ...p, avatarId } : p)),
      },
    },
    ok: true,
  };
}

export function deleteUserProfile(
  state: LauncherState,
  profIdx: number
): { state: LauncherState; ok: boolean } {
  if (state.profiles.items.length <= 1) return { state, ok: false };
  if (profIdx < 0 || profIdx >= state.profiles.items.length) return { state, ok: false };
  if (state.profiles.items[profIdx].isPrimary) return { state, ok: false };
  if (isProfileProtected(state, profIdx)) return { state, ok: false };

  const snapped = snapshotActiveProfile(state);
  const oldActiveId = snapped.profiles.items[snapped.profiles.activeIndex]?.id;
  const deletedActive = profIdx === snapped.profiles.activeIndex;
  const remaining = snapped.profiles.items.filter((_, i) => i !== profIdx);

  let nextActiveIdx = 0;
  if (deletedActive) {
    const primIdx = remaining.findIndex((p) => p.isPrimary);
    nextActiveIdx = primIdx >= 0 ? primIdx : 0;
  } else {
    const found = remaining.findIndex((p) => p.id === oldActiveId);
    nextActiveIdx = found >= 0 ? found : 0;
  }

  const nextBase: LauncherState = {
    ...snapped,
    profiles: {
      ...snapped.profiles,
      items: remaining,
      activeIndex: nextActiveIdx,
    },
  };
  return {
    state: deletedActive ? applyActiveProfile(nextBase) : nextBase,
    ok: true,
  };
}

function compareGamesBySort(
  state: LauncherState,
  idxA: number,
  idxB: number
): number {
  const a = state.games[idxA];
  const b = state.games[idxB];

  if (state.activeCategory === 'RECENT') {
    if (b.lastPlayedRank !== a.lastPlayedRank) return b.lastPlayedRank - a.lastPlayedRank;
    return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
  }

  switch (state.settings.sortOrder) {
    case 'alpha_desc':
      return b.title.localeCompare(a.title, 'fr', { sensitivity: 'base' });
    case 'recent_add':
      if (b.addedOrder !== a.addedOrder) return b.addedOrder - a.addedOrder;
      return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
    case 'oldest_add':
      if (a.addedOrder !== b.addedOrder) return a.addedOrder - b.addedOrder;
      return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
    case 'last_used':
      if (b.lastPlayedRank !== a.lastPlayedRank) return b.lastPlayedRank - a.lastPlayedRank;
      if (b.lastPlayedTs !== a.lastPlayedTs) return b.lastPlayedTs - a.lastPlayedTs;
      return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
    case 'most_launched':
      if (b.launchCount !== a.launchCount) return b.launchCount - a.launchCount;
      if (b.lastPlayedRank !== a.lastPlayedRank) return b.lastPlayedRank - a.lastPlayedRank;
      return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
    case 'play_time':
      if (b.playTimeSec !== a.playTimeSec) return b.playTimeSec - a.playTimeSec;
      if (b.launchCount !== a.launchCount) return b.launchCount - a.launchCount;
      return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
    case 'alpha_asc':
    default:
      return a.title.localeCompare(b.title, 'fr', { sensitivity: 'base' });
  }
}

export function rebuildFilter(state: LauncherState): LauncherState {
  const filteredIndices: number[] = [];
  const q = state.sui.searchActive ? state.sui.searchQuery.toLowerCase() : '';

  for (let i = 0; i < state.games.length; i++) {
    const g = state.games[i];
    let match = false;
    if (state.activeCategory === 'ALL') match = true;
    else if (state.activeCategory === 'FAVORITES') match = g.isFavorite;
    else if (state.activeCategory === 'RECENT') match = g.lastPlayedRank > 0;

    if (match && q.length > 0) {
      if (!g.title.toLowerCase().includes(q) && !g.filename.toLowerCase().includes(q)) {
        match = false;
      }
    }

    if (match) {
      filteredIndices.push(i);
    }
  }

  filteredIndices.sort((a, b) => compareGamesBySort(state, a, b));

  const m = getLayoutMetrics(
    state.settings.layout,
    state.settings.showNames,
    state.settings.showShortcuts
  );
  let selectedPos = state.selectedPos;
  let totalPages = 1;
  let currentPage = 0;

  if (filteredIndices.length === 0) {
    selectedPos = 0;
    currentPage = 0;
    totalPages = 1;
  } else {
    if (selectedPos >= filteredIndices.length) {
      selectedPos = filteredIndices.length - 1;
    }
    if (selectedPos < 0) selectedPos = 0;
    totalPages = Math.ceil(filteredIndices.length / m.perPage);
    currentPage = Math.floor(selectedPos / m.perPage);
  }

  return {
    ...state,
    filteredIndices,
    selectedPos,
    currentPage,
    totalPages,
  };
}

export interface PaginationIndicatorItem {
  pageIndex: number;
  mode: 'dots' | 'dashes';
  x: number;
  y: number;
  width: number;
  height: number;
  radius: number;
  activeWeight: number;
  isActive: boolean;
}

/**
 * Exact TypeScript mirror of native C smart adaptive pagination:
 * - While totalPages <= 19 (NL_PAGINATION_MAX_DOTS): renders circular 8x8 dots & 18x8 active capsule
 * - When totalPages > 19: switches seamlessly to compact, evenly spaced horizontal dashes
 *   centered inside the safe [20..300] width band so every page remains represented without truncation.
 */
export function computePaginationIndicators(
  currentPage: number,
  totalPages: number,
  dotPos?: number,
  paginationY = NL_PAGINATION_Y
): PaginationIndicatorItem[] {
  const safeTotal = Math.max(1, totalPages);
  const safePage = Math.max(0, Math.min(safeTotal - 1, currentPage));

  const rawPos = dotPos !== undefined ? dotPos : safePage;
  const animPos = safeTotal <= 1 ? 0 : Math.max(0, Math.min(safeTotal - 1, rawPos));
  const baseIdx = Math.min(safeTotal - 1, Math.floor(animPos));
  const frac = animPos - baseIdx;

  const items: PaginationIndicatorItem[] = [];

  if (safeTotal <= NL_PAGINATION_MAX_DOTS) {
    const dotD = 8;
    const capW = 18;
    const gap = 6;
    const extraTotal = capW - dotD;
    const extraRight = Math.round(extraTotal * frac);
    const extraLeft = extraTotal - extraRight;

    const totalW = (safeTotal - 1) * (dotD + gap) + capW;
    let curX = Math.floor((NL_SCREEN_W - totalW) / 2);

    for (let i = 0; i < safeTotal; i++) {
      let pw = dotD;
      let weight = 0;
      if (i === baseIdx) {
        pw += extraLeft;
        weight = 1 - frac;
      } else if (frac > 0 && i === baseIdx + 1) {
        pw += extraRight;
        weight = frac;
      }
      items.push({
        pageIndex: i,
        mode: 'dots',
        x: curX,
        y: paginationY,
        width: pw,
        height: dotD,
        radius: 4,
        activeWeight: weight,
        isActive: i === safePage,
      });
      curX += pw + gap;
    }
    return items;
  }

  /* Compact horizontal dashes mode (> 19 pages) */
  const maxRowW = 280;
  const activeExtra = safeTotal <= 36 ? 6 : safeTotal <= 64 ? 4 : 2;
  let gap = safeTotal <= 32 ? 4 : safeTotal <= 56 ? 3 : safeTotal <= 84 ? 2 : 1;
  const avail = maxRowW - (safeTotal - 1) * gap - activeExtra;
  const dashW = Math.max(1, Math.min(10, Math.floor(avail / safeTotal)));

  let totalW = safeTotal * dashW + (safeTotal - 1) * gap + activeExtra;
  if (totalW > maxRowW && gap > 1) {
    gap--;
    totalW = safeTotal * dashW + (safeTotal - 1) * gap + activeExtra;
  }

  let curX = Math.floor((NL_SCREEN_W - totalW) / 2);
  const extraRight = Math.round(activeExtra * frac);
  const extraLeft = activeExtra - extraRight;

  for (let i = 0; i < safeTotal; i++) {
    let pw = dashW;
    let weight = 0;
    if (i === baseIdx) {
      pw += extraLeft;
      weight = 1 - frac;
    } else if (frac > 0 && i === baseIdx + 1) {
      pw += extraRight;
      weight = frac;
    }
    const dh = weight >= 0.5 ? 5 : weight >= 0.18 ? 4 : 3;
    const dy = paginationY + Math.floor((8 - dh) / 2);
    items.push({
      pageIndex: i,
      mode: 'dashes',
      x: curX,
      y: dy,
      width: pw,
      height: dh,
      radius: Math.min(2, Math.floor(dh / 2)),
      activeWeight: weight,
      isActive: i === safePage,
    });
    curX += pw + gap;
  }
  return items;
}

/**
 * Compute subtle neutral navigation luminosity strength (0..1) synchronized
 * directly with the existing scale animation, and strictly disabled during launch zoom.
 */
export function computeNavHighlightStrength(scale: number, launchAnimActive: boolean): number {
  if (launchAnimActive || scale <= NL_SCALE_NORMAL) return 0;
  const span = NL_SCALE_SELECTED - NL_SCALE_NORMAL;
  if (span <= 0) return 0;
  return Math.max(0, Math.min(1, (scale - NL_SCALE_NORMAL) / span));
}

/**
 * Assign a real captured frame (during gameplay) or imported .bmp.tns cover.
 * Rule: Automatic captures NEVER overwrite an existing valid cover!
 */
export function applyCapturedGameIcon(
  state: LauncherState,
  gameIndex: number,
  iconDataUrl: string,
  source: IconSource = 'CAPTURED',
  preserveRunningState = false
): LauncherState {
  if (gameIndex < 0 || gameIndex >= state.games.length) return state;

  const nextRunningIndex = preserveRunningState ? state.runningGameIndex : null;
  const existing = state.games[gameIndex];
  if (source === 'CAPTURED' && existing.iconSource !== 'NONE' && existing.capturedIconDataUrl) {
    /* Still ensure real screenshot is recorded if missing */
    if (!existing.hasScreenshot || !existing.screenshotDataUrl) {
      const updated = state.games.map((g, idx) =>
        idx === gameIndex
          ? { ...g, hasScreenshot: true, screenshotDataUrl: iconDataUrl }
          : g
      );
      return {
        ...state,
        games: updated,
        runningGameIndex: nextRunningIndex,
        lastExecStatus: 0,
      };
    }
    return {
      ...state,
      runningGameIndex: nextRunningIndex,
      lastExecStatus: 0,
    };
  }

  const updatedGames = state.games.map((g, idx) =>
    idx === gameIndex
      ? {
          ...g,
          iconSource: source,
          capturedIconDataUrl: iconDataUrl,
          hasScreenshot: source === 'CAPTURED' ? true : g.hasScreenshot,
          screenshotDataUrl:
            source === 'CAPTURED' && !g.screenshotDataUrl ? iconDataUrl : g.screenshotDataUrl,
        }
      : g
  );
  return {
    ...state,
    games: updatedGames,
    runningGameIndex: nextRunningIndex,
    lastExecStatus: 0,
    toastMessage:
      source === 'BMP' ? 'JAQUETTE .BMP.TNS CHARGEE' : 'JAQUETTE CAPTUREE !',
    toastSeq: state.toastSeq + 1,
  };
}

/**
 * Assign a real captured gameplay screenshot (.scr.tns) for the launch crossfade transition.
 * Never overwrites an existing valid screenshot!
 */
export function applyCapturedScreenshot(
  state: LauncherState,
  gameIndex: number,
  screenshotDataUrl: string,
  preserveRunningState = true
): LauncherState {
  if (gameIndex < 0 || gameIndex >= state.games.length || !screenshotDataUrl) return state;
  const nextRunningIndex = preserveRunningState ? state.runningGameIndex : null;
  const existing = state.games[gameIndex];
  if (existing.hasScreenshot && existing.screenshotDataUrl) {
    return {
      ...state,
      runningGameIndex: nextRunningIndex,
    };
  }

  const updatedGames = state.games.map((g, idx) =>
    idx === gameIndex
      ? {
          ...g,
          hasScreenshot: true,
          screenshotDataUrl,
        }
      : g
  );
  return {
    ...state,
    games: updatedGames,
    runningGameIndex: nextRunningIndex,
    toastMessage: 'CAPTURE .SCR.TNS ENREGISTREE',
    toastSeq: state.toastSeq + 1,
  };
}

/**
 * Exact TypeScript mirror of native C `nl_delete_custom_icon`:
 * Deletes ONLY the custom icon (.bmp.tns / .ico.tns) of the specified game,
 * reverting it immediately to the default monogram emblem (`NONE`) while
 * strictly preserving the game executable (.tns) and its real screenshot (.scr.tns).
 */
export function deleteCustomIcon(state: LauncherState, gameIndex: number): LauncherState {
  if (gameIndex < 0 || gameIndex >= state.games.length) return state;
  const target = state.games[gameIndex];
  if (target.iconSource === 'NONE' && !target.capturedIconDataUrl) {
    return state;
  }

  const updatedGames = state.games.map((g, idx) =>
    idx === gameIndex
      ? {
          ...g,
          iconSource: 'NONE' as IconSource,
          capturedIconDataUrl: null,
          /* Strictly preserve .scr.tns screenshot and .tns game! */
          hasScreenshot: g.hasScreenshot,
          screenshotDataUrl: g.screenshotDataUrl,
        }
      : g
  );

  return {
    ...state,
    games: updatedGames,
    sui: {
      ...state.sui,
      iconConfirmModal: false,
      iconConfirmYes: false,
    },
    toastMessage: 'ICONE SUPPRIMEE',
    toastSeq: state.toastSeq + 1,
  };
}

export function initBatchCapture(state: LauncherState): LauncherState {
  const queue: number[] = [];
  for (let i = 0; i < state.games.length; i++) {
    if (!state.games[i].hasScreenshot || !state.games[i].screenshotDataUrl) {
      queue.push(i);
    }
  }
  const active = queue.length > 0;
  return {
    ...state,
    sui: {
      ...state.sui,
      screen: 'SETTINGS_CAPTURE_BATCH',
      capQueue: queue,
      capTotal: queue.length,
      capPos: 0,
      capDoneCount: 0,
      capSkipCount: 0,
      capCancelled: false,
      capSemiAuto: false,
      capActive: active,
    },
    toastMessage: active ? null : 'TOUTES LES CAPTURES SONT PRETES',
    toastSeq: active ? state.toastSeq : state.toastSeq + 1,
  };
}

export function advanceBatchCapture(
  state: LauncherState,
  capturedOk: boolean,
  wasSkipped: boolean,
  triggerSemiAuto = false
): LauncherState {
  const sui = state.sui;
  if (triggerSemiAuto && !capturedOk && !wasSkipped) {
    return {
      ...state,
      sui: {
        ...sui,
        capSemiAuto: true,
      },
      toastMessage: 'MODE SEMI-AUTO : PASSER OU ESC',
      toastSeq: state.toastSeq + 1,
    };
  }

  const nextDone = sui.capDoneCount + (capturedOk ? 1 : 0);
  const nextSkip = sui.capSkipCount + (wasSkipped ? 1 : 0);
  const nextPos = sui.capPos + 1;
  const stillActive = nextPos < sui.capTotal;

  return {
    ...state,
    sui: {
      ...sui,
      capDoneCount: nextDone,
      capSkipCount: nextSkip,
      capPos: nextPos,
      capSemiAuto: false,
      capActive: stillActive,
    },
    toastMessage: stillActive ? state.toastMessage : 'CAPTURES TERMINEES !',
    toastSeq: stillActive ? state.toastSeq : state.toastSeq + 1,
  };
}

export function cancelBatchCapture(state: LauncherState): LauncherState {
  return {
    ...state,
    sui: {
      ...state.sui,
      capActive: false,
      capSemiAuto: false,
      capCancelled: true,
    },
    toastMessage: 'CAPTURE INTERROMPUE',
    toastSeq: state.toastSeq + 1,
  };
}

function adjustScroll(cursor: number, scroll: number, visibleRows: number): number {
  let next = scroll;
  if (cursor < next) next = cursor;
  else if (cursor >= next + visibleRows) next = cursor - visibleRows + 1;
  return Math.max(0, next);
}

export function resetSettingsOnly(state: LauncherState): LauncherState {
  const cleanedGames = state.games.map((g) => ({
    ...g,
    shortcutKey: '',
  }));
  const next: LauncherState = {
    ...state,
    displayMode: 'dark',
    accentColor: 'blue',
    selectedPos: 0,
    settings: {
      autostart: false,
      globalHotkey: false,
      hotkeyCombo: 'ctrl_menu',
      layout: '3x2',
      scrollDir: 'vertical',
      appOpenAnim: 'current',
      appCloseAnim: 'liquid',
      launchAnimMode: 'current',
      returnAnimMode: 'liquid',
      showNames: false,
      showShortcuts: true,
      showBattery: false,
      profileLockEnabled: false,
      touchpadMode: 'touchpad',
      touchpadNav: false,
      touchpadSensitivity: NL_TP_SENS_DEFAULT,
      cursorSpeed: NL_CURSOR_SPEED_DEFAULT,
      touchpadCursorShow: false,
      touchpadCursorStyle: 'arrow',
      cursorSize: NL_CURSOR_SIZE_DEFAULT,
      tapToClick: false,
      sortOrder: 'alpha_asc',
    },
    sys: {
      ...state.sys,
      autostartActive: false,
      autostartDiag: 'Demarrage automatique supprime',
      hotkeyActive: false,
      hotkeyRamActive: false,
      hotkeyDiag: 'Raccourci global desactive',
      bootRecoveryTriggered: false,
      bootRecoveryReason: 'none',
    },
    games: cleanedGames,
    sui: {
      ...state.sui,
      screen: 'SETTINGS_MAIN',
      hotkeyCursor: 0,
      tpCursor: 0,
      tpScroll: 0,
      tpTab: 0,
      tpCustomRow: 0,
      tpStyleCursor: 0,
      tpSizeCursor: cursorSizeToIndex(NL_CURSOR_SIZE_DEFAULT),
      resetConfirmYes: false,
      resetPerformed: true,
      hoverValid: false,
      hoverKind: 'NONE',
      hoverIndex: 0,
      hoverSub: 0,
    },
    toastMessage: 'PARAMETRES REINITIALISES',
    toastSeq: state.toastSeq + 1,
  };
  return updateMouseHover(snapshotActiveProfile(rebuildFilter(next)));
}

export function runSimulatorRescan(
  state: LauncherState,
  options?: { simulatedEmpty?: boolean; addedCount?: number; removedCount?: number }
): LauncherState {
  const prevCount = state.games.length;
  const folderSet = new Set<string>();
  for (const g of state.games) {
    const slash = g.path.lastIndexOf('/');
    if (slash > 0) {
      folderSet.add(g.path.slice(0, slash));
    }
  }
  const iconsLoaded = state.games.filter(
    (g) => g.iconSource !== 'NONE' && Boolean(g.capturedIconDataUrl)
  ).length;
  const iconsFallback = Math.max(0, prevCount - iconsLoaded);
  const rescanCount = (state.sui.rescan?.rescanCount ?? 0) + 1;

  if (options?.simulatedEmpty && prevCount > 0) {
    return {
      ...state,
      sui: {
        ...state.sui,
        screen: 'SETTINGS_RESCAN',
        rescan: {
          dirsScanned: Math.max(1, folderSet.size + 1),
          filesExamined: Math.max(1, state.stats.scannedFiles),
          gamesFound: 0,
          gamesAdded: 0,
          gamesKept: prevCount,
          gamesRemoved: 0,
          iconsLoaded,
          iconsFallback,
          memorizedFolders: Math.max(1, folderSet.size),
          emptyConfirmPending: true,
          emptyConfirmReplace: false,
          finished: false,
          rescanCount,
        },
      },
      toastMessage: '0 JEU DETECTE — CONFIRMATION',
      toastSeq: state.toastSeq + 1,
    };
  }

  const added = Math.max(0, options?.addedCount ?? 0);
  const removed = Math.max(0, Math.min(prevCount, options?.removedCount ?? 0));
  const kept = Math.max(0, prevCount - removed);
  const found = kept + added;

  return rebuildFilter({
    ...state,
    sui: {
      ...state.sui,
      screen: 'SETTINGS_RESCAN',
      rescan: {
        dirsScanned: Math.max(1, folderSet.size + 2),
        filesExamined: Math.max(found, state.stats.scannedFiles),
        gamesFound: found,
        gamesAdded: added,
        gamesKept: kept,
        gamesRemoved: removed,
        iconsLoaded,
        iconsFallback,
        memorizedFolders: Math.max(1, folderSet.size),
        emptyConfirmPending: false,
        emptyConfirmReplace: false,
        finished: true,
        rescanCount,
      },
    },
    sys: {
      ...state.sys,
      cacheValid: true,
      cacheCrc32: computeLibraryCacheCrc32(state.games),
    },
    toastMessage: `RESCAN OK : ${found} JEU(X)`,
    toastSeq: state.toastSeq + 1,
  });
}

function triggerGameLaunch(
  state: LauncherState,
  gameIdx: number,
  nowSec: number
): LauncherState {
  if (gameIdx < 0 || gameIdx >= state.games.length) return state;
  const m = getLayoutMetrics(
    state.settings.layout,
    state.settings.showNames,
    state.settings.showShortcuts
  );
  let launchPos = state.selectedPos;
  if (
    launchPos < 0 ||
    launchPos >= state.filteredIndices.length ||
    state.filteredIndices[launchPos] !== gameIdx
  ) {
    const foundSlot = state.filteredIndices.indexOf(gameIdx);
    if (foundSlot >= 0) {
      launchPos = foundSlot;
    }
  }
  const nextCounter = state.playCounter + 1;
  const ts = nowSec > 0 ? Math.floor(nowSec) : Math.floor(Date.now() / 1000);
  const updatedGames = state.games.map((g, idx) =>
    idx === gameIdx
      ? {
          ...g,
          lastPlayedRank: nextCounter,
          launchCount: g.launchCount + 1,
          lastPlayedTs: ts,
        }
      : g
  );
  /* Never call rebuildFilter on launch: filteredIndices must keep its exact order
   * throughout the launch animation and while the game is running. */
  return snapshotActiveProfile({
    ...state,
    games: updatedGames,
    playCounter: nextCounter,
    selectedPos: launchPos,
    currentPage: Math.floor(launchPos / m.perPage),
    runningGameIndex: gameIdx,
    runningStartMs: Date.now(),
    sui: {
      ...state.sui,
      screen: 'LIBRARY',
      qpOpen: false,
      scPickingKey: false,
      scConflictPrompt: false,
    },
  });
}

function assignShortcutKeyToGame(
  state: LauncherState,
  gameIdx: number,
  keyChar: string,
  forceReplace: boolean
): LauncherState {
  if (gameIdx < 0 || gameIdx >= state.games.length) return state;
  if (isProfileProtected(state, state.profiles.activeIndex)) {
    return {
      ...state,
      toastMessage: 'Profil verrouille',
      toastSeq: state.toastSeq + 1,
    };
  }
  const u = keyChar.toUpperCase();
  if (isShortcutReserved(u)) {
    return {
      ...state,
      toastMessage: 'TOUCHE SYSTEME RESERVEE',
      toastSeq: state.toastSeq + 1,
    };
  }
  if (!isShortcutAssignable(u)) {
    return {
      ...state,
      toastMessage: 'TOUCHE NON SUPPORTEE',
      toastSeq: state.toastSeq + 1,
    };
  }
  const owner = findShortcutOwner(state, u, gameIdx);
  if (owner >= 0 && !forceReplace) {
    return {
      ...state,
      sui: {
        ...state.sui,
        scConflictPrompt: true,
        scConflictKey: u,
        scConflictOwnerIdx: owner,
        scConflictConfirmYes: false,
      },
    };
  }
  const updatedGames = state.games.map((g, i) => {
    if (i === owner) return { ...g, shortcutKey: '' };
    if (i === gameIdx) return { ...g, shortcutKey: u };
    return g;
  });
  return snapshotActiveProfile({
    ...state,
    games: updatedGames,
    sui: {
      ...state.sui,
      scPickingKey: false,
      scConflictPrompt: false,
    },
    toastMessage: `RACCOURCI [${u}] ENREGISTRE`,
    toastSeq: state.toastSeq + 1,
  });
}

export function toggleSimulatedNdlessPersistence(state: LauncherState): LauncherState {
  const nextPersistent = !state.sys.ndlessPersistent;
  return {
    ...state,
    settings: {
      ...state.settings,
      autostart: false,
    },
    sys: {
      ...state.sys,
      ndlessPersistent: nextPersistent,
      autostartActive: false,
      bootRecoveryTriggered: false,
      bootRecoveryReason: 'none',
      autostartDiag: nextPersistent
        ? 'Ndless persistant (lancement manuel uniquement)'
        : 'Ndless non persistant (lancement manuel)',
    },
  };
}

export interface SimulateBootOptions {
  isStartup?: boolean;
  recoveryKeyHeld?: boolean;
  targetExists?: boolean;
  simulateMode?: 'clean' | 'early_clean_exit' | 'crash_mid_init' | 'exec_failed' | 'early_error_return';
}

export interface SimulateBootResult {
  outcome: BootSimulationOutcome;
  execCalled: boolean;
  pendingSeenInExec: number | null;
  state: LauncherState;
  guard: BootGuardState;
}

export function getBootRecoveryReasonLabel(reason: BootRecoveryReason): string {
  switch (reason) {
    case 'crash_detected':
      return 'Crash au demarrage';
    case 'manual_key':
      return 'Touche ESC/ON maintenue';
    case 'missing_target':
      return 'N-Launcher.tns introuvable';
    case 'exec_failed':
      return 'Echec execution .tns';
    default:
      return 'Anomalie au demarrage';
  }
}

function applyRecoveryDisableToState(
  state: LauncherState,
  reason: BootRecoveryReason
): { state: LauncherState; guard: BootGuardState } {
  const nextGuard: BootGuardState = {
    pending: 0,
    recovery: 1,
    disabled: 1,
    reason,
  };
  const nextState: LauncherState = {
    ...state,
    settings: {
      ...state.settings,
      autostart: false,
      globalHotkey: false,
    },
    sys: {
      ...state.sys,
      autostartActive: false,
      autostartDiag: 'Mode securise actif (reprise OS)',
      hotkeyActive: false,
      hotkeyRamActive: false,
      hotkeyDiag: 'Desactive par mode securise',
      bootRecoveryTriggered: true,
      bootRecoveryReason: reason,
    },
  };
  return { state: nextState, guard: nextGuard };
}

/**
 * Exact TypeScript mirror of native `nl_boot_stub_execute()` + deferred stabilization in `main_ndless.c`:
 * - Checks ESC/ON recovery key first
 * - Checks crash-loop pending flag BEFORE invoking N-Launcher
 * - Writes pending=1 BEFORE entering N-Launcher, and clears pending=0 ONLY after stabilization
 *   (NL_BOOT_STABILIZE_FRAMES = 30) or clean ESC/PowerOff exit
 * - Never wipes user preferences, themes, favorites, play stats, shortcuts, or icons
 */
export function simulateAutostartBoot(
  state: LauncherState,
  guard: BootGuardState,
  options: SimulateBootOptions = {}
): SimulateBootResult {
  const isStartup = options.isStartup ?? true;
  const recoveryKeyHeld = options.recoveryKeyHeld ?? false;
  const targetExists = options.targetExists ?? true;
  const mode = options.simulateMode ?? 'clean';

  if (!isStartup) {
    return {
      outcome: 'SKIP_NOT_STARTUP',
      execCalled: false,
      pendingSeenInExec: null,
      state,
      guard,
    };
  }

  if (recoveryKeyHeld) {
    if (state.settings.autostart || state.sys.autostartActive) {
      const res = applyRecoveryDisableToState(state, 'manual_key');
      return {
        outcome: 'ABORT_RECOVERY_KEY',
        execCalled: false,
        pendingSeenInExec: null,
        state: res.state,
        guard: res.guard,
      };
    }
    return {
      outcome: 'ABORT_RECOVERY_KEY',
      execCalled: false,
      pendingSeenInExec: null,
      state,
      guard,
    };
  }

  if (!state.settings.autostart || !state.sys.hasNdless || !state.sys.ndlessPersistent) {
    return {
      outcome: 'SKIP_DISABLED',
      execCalled: false,
      pendingSeenInExec: null,
      state,
      guard,
    };
  }

  if (guard.pending !== 0) {
    const res = applyRecoveryDisableToState(state, 'crash_detected');
    return {
      outcome: 'ABORT_CRASH_GUARD',
      execCalled: false,
      pendingSeenInExec: null,
      state: res.state,
      guard: res.guard,
    };
  }

  if (!targetExists) {
    const res = applyRecoveryDisableToState(state, 'missing_target');
    return {
      outcome: 'ABORT_MISSING_TARGET',
      execCalled: false,
      pendingSeenInExec: null,
      state: res.state,
      guard: res.guard,
    };
  }

  /* Persist pending=1 BEFORE executing N-Launcher */
  const pendingGuard: BootGuardState = {
    pending: 1,
    recovery: 0,
    disabled: 0,
    reason: 'booting',
  };

  if (mode === 'exec_failed') {
    const res = applyRecoveryDisableToState(state, 'exec_failed');
    return {
      outcome: 'ABORT_EXEC_FAILED',
      execCalled: true,
      pendingSeenInExec: pendingGuard.pending,
      state: res.state,
      guard: res.guard,
    };
  }

  if (mode === 'crash_mid_init') {
    /* Hardware reboot mid-init before NL_BOOT_STABILIZE_FRAMES: pending=1 remains stored on flash */
    return {
      outcome: 'CRASHED_MID_INIT',
      execCalled: true,
      pendingSeenInExec: pendingGuard.pending,
      state,
      guard: pendingGuard,
    };
  }

  if (mode === 'early_error_return') {
    const res = applyRecoveryDisableToState(state, 'crash_detected');
    return {
      outcome: 'ABORT_CRASH_GUARD',
      execCalled: true,
      pendingSeenInExec: pendingGuard.pending,
      state: res.state,
      guard: res.guard,
    };
  }

  /* Clean stabilization at frame 30 OR clean ESC/PowerOff exit preserves autostart=true & clears pending=0 */
  const cleanState: LauncherState = {
    ...state,
    sys: {
      ...state.sys,
      autostartActive: true,
      autostartDiag: 'Lancement auto actif (nl_boot.tns)',
      bootRecoveryTriggered: false,
      bootRecoveryReason: 'none',
    },
  };
  return {
    outcome: 'OK_CLEAN',
    execCalled: true,
    pendingSeenInExec: pendingGuard.pending,
    state: cleanState,
    guard: createCleanBootGuardState(),
  };
}

function rgb565Pack(r8: number, g8: number, b8: number): number {
  return (((r8 >> 3) & 0x1f) << 11) | (((g8 >> 2) & 0x3f) << 5) | ((b8 >> 3) & 0x1f);
}

function rgb565Unpack(px: number): { r8: number; g8: number; b8: number; luma: number } {
  const r5 = (px >> 11) & 0x1f;
  const g6 = (px >> 5) & 0x3f;
  const b5 = px & 0x1f;
  const r8 = (r5 * 527 + 23) >> 6;
  const g8 = (g6 * 259 + 33) >> 6;
  const b8 = (b5 * 527 + 23) >> 6;
  const luma = (r8 * 77 + g8 * 150 + b8 * 29) >> 8;
  return { r8, g8, b8, luma };
}

function sampleVramPixel565(
  vram16: Uint16Array,
  rotated240x320: boolean,
  x: number,
  y: number
): number {
  if (x < 0 || x >= NL_SCREEN_W || y < 0 || y >= NL_SCREEN_H) return 0;
  const idx = rotated240x320 ? x * NL_SCREEN_H + y : y * NL_SCREEN_W + x;
  return vram16[idx] ?? 0;
}

function putVramPixel565(
  vram16: Uint16Array,
  rotated240x320: boolean,
  x: number,
  y: number,
  c: number
): void {
  if (x < 0 || x >= NL_SCREEN_W || y < 0 || y >= NL_SCREEN_H) return;
  const idx = rotated240x320 ? x * NL_SCREEN_H + y : y * NL_SCREEN_W + x;
  vram16[idx] = c;
}

function detectBatteryRoiPass(vram16: Uint16Array, rotated240x320: boolean): BatteryLevel {
  const SENTINEL = 0xa55a;
  let bgLumaSum = 0;
  let bgSamples = 0;
  let sentinelHits = 0;

  for (let x = 268; x <= 318; x += 2) {
    const pTop = sampleVramPixel565(vram16, rotated240x320, x, 1);
    const pBot = sampleVramPixel565(vram16, rotated240x320, x, 21);
    if (pTop === SENTINEL) sentinelHits++;
    if (pBot === SENTINEL) sentinelHits++;
    const u1 = rgb565Unpack(pTop);
    const u2 = rgb565Unpack(pBot);
    bgLumaSum += u1.luma + u2.luma;
    bgSamples += 2;
  }

  if (bgSamples <= 0 || sentinelHits > Math.floor(bgSamples / 3)) {
    return 'UNAVAILABLE';
  }
  const bgLuma = Math.floor(bgLumaSum / bgSamples);

  let minX = 999;
  let maxX = -1;
  let minY = 999;
  let maxY = -1;
  let fgTotal = 0;

  for (let y = 2; y <= 20; y++) {
    for (let x = 272; x <= 317; x++) {
      const px = sampleVramPixel565(vram16, rotated240x320, x, y);
      if (px === SENTINEL) continue;
      const { r8, g8, b8, luma } = rgb565Unpack(px);
      const maxC = Math.max(r8, g8, b8);
      const minC = Math.min(r8, g8, b8);
      const sat = maxC - minC;
      const diffBg = Math.abs(luma - bgLuma);

      if (diffBg >= 38 || sat >= 45) {
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        fgTotal++;
      }
    }
  }

  if (maxX < minX || maxY < minY || fgTotal < 24) {
    return 'UNAVAILABLE';
  }

  const bw = maxX - minX + 1;
  const bh = maxY - minY + 1;
  if (bw < 14 || bw > 42 || bh < 7 || bh > 17 || bw < bh + 3) {
    return 'UNAVAILABLE';
  }

  const bodyW = bw - 2;
  let borderHits = 0;
  for (let x = minX + 1; x < minX + bodyW - 1; x++) {
    const pt = sampleVramPixel565(vram16, rotated240x320, x, minY);
    const pb = sampleVramPixel565(vram16, rotated240x320, x, maxY);
    const lt = rgb565Unpack(pt).luma;
    const lb = rgb565Unpack(pb).luma;
    if (Math.abs(lt - bgLuma) >= 34) borderHits++;
    if (Math.abs(lb - bgLuma) >= 34) borderHits++;
  }
  if (borderHits < bodyW - 2) {
    return 'UNAVAILABLE';
  }

  const ix0 = minX + 2;
  const ix1 = maxX - 4;
  const iy0 = minY + 2;
  const iy1 = maxY - 2;
  if (ix1 < ix0 || iy1 < iy0) {
    return 'UNAVAILABLE';
  }

  const totalCols = ix1 - ix0 + 1;
  const colH = iy1 - iy0 + 1;
  let filledCols = 0;
  let redAlertPixels = 0;

  for (let x = ix0; x <= ix1; x++) {
    let activeRows = 0;
    for (let y = iy0; y <= iy1; y++) {
      const px = sampleVramPixel565(vram16, rotated240x320, x, y);
      const { r8, g8, b8, luma } = rgb565Unpack(px);

      if (r8 >= 170 && g8 <= 105 && r8 > g8 + 75 && b8 <= 95) {
        redAlertPixels++;
        activeRows++;
        continue;
      }

      const isGreen = g8 >= 105 && g8 > r8 + 18 && g8 > b8 + 18;
      const isContrasted = Math.abs(luma - bgLuma) >= 42;
      if (isGreen || isContrasted) {
        activeRows++;
      }
    }
    if (activeRows * 2 >= colH) {
      filledCols++;
    }
  }

  if (redAlertPixels >= 3) {
    return 'LOW';
  }

  const fillPct = Math.floor((filledCols * 100) / totalCols);
  if (fillPct <= 34) return 'LOW';
  if (fillPct <= 67) return 'MEDIUM';
  return 'HIGH';
}

/**
 * Exact TypeScript mirror of native C `nl_detect_battery_from_vram()` (Solution A):
 * Analyzes only the top-right TI-OS status bar ROI (x in [268..318], y in [1..22]).
 */
export function detectBatteryFromVram(
  vram16: Uint16Array,
  rotated240x320 = true
): BatteryLevel {
  if (!vram16 || vram16.length < NL_SCREEN_W * NL_SCREEN_H) {
    return 'UNAVAILABLE';
  }
  const primary = detectBatteryRoiPass(vram16, rotated240x320);
  if (primary !== 'UNAVAILABLE') return primary;
  return detectBatteryRoiPass(vram16, !rotated240x320);
}

/**
 * Exact TypeScript mirror of native C `nl_stamp_tios_battery_icon()`:
 * Renders a synthetic TI-OS top-right battery icon into a 320x240 / 240x320 RGB565 buffer.
 */
export function stampTiosBatteryIcon(
  vram16: Uint16Array,
  rotated240x320: boolean,
  level: BatteryLevel,
  useRedAlert = false
): void {
  if (!vram16 || vram16.length < NL_SCREEN_W * NL_SCREEN_H) return;
  const barBg = rgb565Pack(28, 34, 44);
  for (let y = 0; y <= 23; y++) {
    for (let x = 264; x < NL_SCREEN_W; x++) {
      putVramPixel565(vram16, rotated240x320, x, y, barBg);
    }
  }

  if (level === 'UNAVAILABLE') return;

  const borderCol = rgb565Pack(235, 240, 248);
  for (let x = 286; x <= 309; x++) {
    putVramPixel565(vram16, rotated240x320, x, 5, borderCol);
    putVramPixel565(vram16, rotated240x320, x, 15, borderCol);
  }
  for (let y = 5; y <= 15; y++) {
    putVramPixel565(vram16, rotated240x320, 286, y, borderCol);
    putVramPixel565(vram16, rotated240x320, 287, y, borderCol);
    putVramPixel565(vram16, rotated240x320, 308, y, borderCol);
    putVramPixel565(vram16, rotated240x320, 309, y, borderCol);
  }
  for (let y = 8; y <= 12; y++) {
    putVramPixel565(vram16, rotated240x320, 310, y, borderCol);
    putVramPixel565(vram16, rotated240x320, 311, y, borderCol);
  }

  let fillCols = 0;
  let fillCol = rgb565Pack(46, 204, 96);
  if (level === 'HIGH') {
    fillCols = 18;
    fillCol = rgb565Pack(46, 204, 96);
  } else if (level === 'MEDIUM') {
    fillCols = 10;
    fillCol = rgb565Pack(234, 179, 8);
  } else if (level === 'LOW') {
    fillCols = useRedAlert ? 8 : 4;
    fillCol = useRedAlert ? rgb565Pack(235, 52, 46) : rgb565Pack(220, 160, 40);
  }

  for (let x = 288; x < 288 + fillCols && x <= 307; x++) {
    for (let y = 7; y <= 13; y++) {
      putVramPixel565(vram16, rotated240x320, x, y, fillCol);
    }
  }
}

/**
 * Exact TypeScript mirror of native C `nl_check_periodic_battery_due()`:
 * Returns true if `showBattery` is ON and >= 3600s (60 minutes) have elapsed since last read.
 */
export function checkPeriodicBatteryDue(state: LauncherState, currentRtcSec: number): boolean {
  if (!state.settings.showBattery || currentRtcSec <= 0) return false;
  if (state.sys.batteryLastRtc <= 0) return true;
  return (
    currentRtcSec >= state.sys.batteryLastRtc &&
    currentRtcSec - state.sys.batteryLastRtc >= NL_BATTERY_REFRESH_INTERVAL_SEC
  );
}

/**
 * Performs an optical TI-OS framebuffer read (simulating `refresh_osscr()` + `nl_detect_battery_from_vram`)
 * and updates `sys.batteryLevel`, `sys.batteryLastRtc`, and `sys.batteryReadCount`.
 */
export function refreshBatteryFromTios(
  state: LauncherState,
  nowSec = Math.floor(Date.now() / 1000),
  showToast = false
): LauncherState {
  const vram = new Uint16Array(NL_SCREEN_W * NL_SCREEN_H);
  stampTiosBatteryIcon(vram, true, state.sys.tiosBatterySource, false);
  const detected = detectBatteryFromVram(vram, true);
  return {
    ...state,
    sys: {
      ...state.sys,
      batteryLevel: detected,
      batteryLastRtc: nowSec > 0 ? Math.floor(nowSec) : Math.floor(Date.now() / 1000),
      batteryReadCount: state.sys.batteryReadCount + 1,
    },
    toastMessage: showToast
      ? `Batterie : ${BATTERY_LEVEL_LABELS[detected]}`
      : state.toastMessage,
    toastSeq: showToast ? state.toastSeq + 1 : state.toastSeq,
  };
}

export function setSimulatedTiosBattery(
  state: LauncherState,
  sourceLevel: BatteryLevel,
  immediateRefresh = true,
  nowSec = Math.floor(Date.now() / 1000)
): LauncherState {
  const next: LauncherState = {
    ...state,
    sys: {
      ...state.sys,
      tiosBatterySource: sourceLevel,
    },
  };
  return immediateRefresh ? refreshBatteryFromTios(next, nowSec, true) : next;
}

export function handleCharInput(
  state: LauncherState,
  ch: string,
  nowSec = Math.floor(Date.now() / 1000)
): LauncherState {
  if (state.runningGameIndex !== null || !ch) return state;

  if (
    state.sui.screen === 'SETTINGS_SHORTCUTS' &&
    state.sui.scPickingKey &&
    !state.sui.scConflictPrompt
  ) {
    return assignShortcutKeyToGame(state, state.sui.scGameCursor, ch, false);
  }

  if (state.sui.screen === 'SETTINGS_CAPTURE_BATCH' && (ch === 's' || ch === 'S')) {
    return handleSettingsKey(state, 'RIGHT', nowSec);
  }

  if (
    state.sui.screen === 'SETTINGS_PROFILES' &&
    (state.sui.profMode === 'EDIT_NAME' ||
      state.sui.profMode === 'CREATE_NAME' ||
      state.sui.profMode === 'CREATE_AVATAR')
  ) {
    if (ch === '\b' || ch === '\x7f') {
      return {
        ...state,
        sui: {
          ...state.sui,
          profEditName: state.sui.profEditName.slice(0, -1),
        },
      };
    }
    if (/^[a-zA-Z0-9 _-]$/.test(ch) && state.sui.profEditName.length < NL_PROFILE_NAME_MAX) {
      return {
        ...state,
        sui: {
          ...state.sui,
          profEditName: state.sui.profEditName + ch,
        },
      };
    }
    return state;
  }

  if (state.sui.screen === 'LIBRARY') {
    if (state.sui.qpOpen) {
      if (ch === '.') return handleLauncherKey(state, 'PROFILE_QUICK', nowSec);
      return state;
    }

    if (ch === '5') {
      return handleLauncherKey(state, 'EXE', nowSec);
    }
    if (state.sui.searchActive) {
      if (ch === '\b' || ch === '\x7f') {
        return handleLauncherKey(state, 'BACKSPACE', nowSec);
      }
      if (/^[a-zA-Z0-9 _-]$/.test(ch) && state.sui.searchQuery.length < 24) {
        return rebuildFilter({
          ...state,
          selectedPos: 0,
          navSeq: state.navSeq + 1,
          sui: {
            ...state.sui,
            searchQuery: state.sui.searchQuery + ch,
          },
        });
      }
      return state;
    }

    if (ch === '.') return handleLauncherKey(state, 'PROFILE_QUICK', nowSec);
    if (ch === '0') return handleLauncherKey(state, 'PAGE_PREV', nowSec);
    if (ch === '-') return handleLauncherKey(state, 'PAGE_NEXT', nowSec);

    const u = ch.toUpperCase();
    if (isShortcutAssignable(u)) {
      const owner = findShortcutOwner(state, u, -1);
      if (owner >= 0) {
        return triggerGameLaunch(state, owner, nowSec);
      }
    }
    if (/^[a-zA-Z0-9_]$/.test(ch)) {
      return rebuildFilter({
        ...state,
        selectedPos: 0,
        navSeq: state.navSeq + 1,
        sui: {
          ...state.sui,
          searchActive: true,
          searchQuery: ch,
        },
      });
    }
  }

  return state;
}

export function requestCalculatorPoweroff(state: LauncherState): LauncherState {
  return {
    ...state,
    sui: {
      ...state.sui,
      poweroffRequested: true,
    },
    toastMessage: 'EXTINCTION CALCULATRICE...',
    toastSeq: state.toastSeq + 1,
  };
}

function handleSettingsKey(
  state: LauncherState,
  key: NavKey,
  nowSec: number
): LauncherState {
  const sui = state.sui;

  if (key === 'DOCK') {
    const idx = DISPLAY_MODE_ORDER.indexOf(state.displayMode);
    return {
      ...state,
      displayMode: DISPLAY_MODE_ORDER[(idx + 1) % DISPLAY_MODE_ORDER.length],
    };
  }
  if (key === 'MENU') {
    const idx = ACCENT_COLOR_ORDER.indexOf(state.accentColor);
    return {
      ...state,
      accentColor: ACCENT_COLOR_ORDER[(idx + 1) % ACCENT_COLOR_ORDER.length],
    };
  }
  if (key === 'SETTINGS') {
    return {
      ...state,
      sui: {
        ...sui,
        screen: 'LIBRARY',
        scPickingKey: false,
        scConflictPrompt: false,
        profFromQuick: false,
        profMode: 'LIST',
      },
    };
  }
  if (
    key === 'RESCAN' &&
    sui.screen !== 'SETTINGS_RESCAN' &&
    sui.screen !== 'SETTINGS_SHORTCUTS' &&
    sui.screen !== 'SETTINGS_ICONS' &&
    sui.screen !== 'SETTINGS_PROFILES' &&
    sui.screen !== 'SETTINGS_DIAG'
  ) {
    return requestCalculatorPoweroff(state);
  }

  switch (sui.screen) {
    case 'SETTINGS_MAIN': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'LIBRARY' } };
      }
      if (key === 'UP') {
        const cur = sui.menuCursor > 0 ? sui.menuCursor - 1 : SETTINGS_MENU_ITEMS.length - 1;
        return {
          ...state,
          sui: { ...sui, menuCursor: cur, menuScroll: adjustScroll(cur, sui.menuScroll, 6) },
        };
      }
      if (key === 'DOWN') {
        const cur = (sui.menuCursor + 1) % SETTINGS_MENU_ITEMS.length;
        return {
          ...state,
          sui: { ...sui, menuCursor: cur, menuScroll: adjustScroll(cur, sui.menuScroll, 6) },
        };
      }
      if (key === 'EXE' || key === 'RIGHT') {
        const item = SETTINGS_MENU_ITEMS[sui.menuCursor];
        if (!item) return state;
        if (item.id === 'SETTINGS_REFRESH_LIB') {
          return runSimulatorRescan(state);
        }
        const baseSui: SettingsUiState = {
          ...sui,
          screen: item.id,
          displayCursor: 0,
          displayScroll: 0,
          animCursor: 0,
          hotkeyCursor: 0,
          tpCursor: 0,
          tpScroll: 0,
          tpTab: 0,
          tpCustomRow: 0,
          tpStyleCursor: clampCursorStyleToCard(state.settings.touchpadCursorStyle),
          tpSizeCursor: cursorSizeToIndex(state.settings.cursorSize),
          scGameCursor: 0,
          scGameScroll: 0,
          scPickingKey: false,
          scConflictPrompt: false,
          subCursor: item.id === 'SETTINGS_SORT' ? SORT_ORDER_LIST.indexOf(state.settings.sortOrder) : 0,
          subScroll:
            item.id === 'SETTINGS_SORT'
              ? adjustScroll(Math.max(0, SORT_ORDER_LIST.indexOf(state.settings.sortOrder)), 0, 6)
              : 0,
          resetConfirmYes: false,
          resetPerformed: false,
          iconCursor: 0,
          iconScrollRow: 0,
          iconConfirmModal: false,
          iconConfirmYes: false,
          profMode: 'LIST',
          profCursor: 0,
          profScroll: 0,
          profDetailIdx: state.profiles.activeIndex,
          profDetailCursor: 0,
          profEditName: '',
          profAvatarCat: 0,
          profAvatarCursor: 0,
          profDeleteYes: false,
          profFromQuick: false,
          diagTab: 0,
          diagCursor: 0,
          diagScroll: 0,
          diagDetailOpen: false,
          diagConfirmModal: false,
          diagConfirmYes: false,
          diagClearReq: false,
        };
        if (item.id === 'SETTINGS_CAPTURE_BATCH') {
          return initBatchCapture({ ...state, sui: baseSui });
        }
        return { ...state, sui: baseSui };
      }
      return state;
    }

    case 'SETTINGS_DIAG': {
      const diagCount = state.diag.entries.length;
      if (sui.diagConfirmModal) {
        if (key === 'ESC') {
          return {
            ...state,
            sui: { ...sui, diagConfirmModal: false, diagConfirmYes: false },
          };
        }
        if (key === 'LEFT' || key === 'RIGHT' || key === 'UP' || key === 'DOWN') {
          return {
            ...state,
            sui: { ...sui, diagConfirmYes: !sui.diagConfirmYes },
          };
        }
        if (key === 'EXE') {
          if (sui.diagConfirmYes) {
            return clearDiagLogs(state);
          }
          return {
            ...state,
            sui: { ...sui, diagConfirmModal: false, diagConfirmYes: false },
          };
        }
        return state;
      }

      if (sui.diagDetailOpen) {
        if (key === 'ESC' || key === 'EXE') {
          return { ...state, sui: { ...sui, diagDetailOpen: false } };
        }
        if (key === 'LEFT' || key === 'UP') {
          if (diagCount > 0 && sui.diagCursor > 0) {
            const nextCur = sui.diagCursor - 1;
            return {
              ...state,
              sui: {
                ...sui,
                diagCursor: nextCur,
                diagScroll: adjustScroll(nextCur, sui.diagScroll, 4),
              },
            };
          }
          return state;
        }
        if (key === 'RIGHT' || key === 'DOWN') {
          if (diagCount > 0 && sui.diagCursor + 1 < diagCount) {
            const nextCur = sui.diagCursor + 1;
            return {
              ...state,
              sui: {
                ...sui,
                diagCursor: nextCur,
                diagScroll: adjustScroll(nextCur, sui.diagScroll, 4),
              },
            };
          }
          return state;
        }
        return state;
      }

      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'TAB' || key === 'LEFT' || key === 'RIGHT') {
        return {
          ...state,
          sui: {
            ...sui,
            diagTab: sui.diagTab === 0 ? 1 : 0,
            diagCursor: 0,
            diagScroll: 0,
            diagDetailOpen: false,
          },
        };
      }
      if (sui.diagTab === 0) {
        const maxIdx = diagCount; // 0..diagCount-1 = entries, diagCount = Clear button
        if (key === 'RESCAN' || key === 'BACKSPACE') {
          return {
            ...state,
            sui: {
              ...sui,
              diagConfirmModal: true,
              diagConfirmYes: false, // Safe Annuler default
            },
          };
        }
        if (key === 'UP') {
          const nextCur = sui.diagCursor > 0 ? sui.diagCursor - 1 : maxIdx;
          const listCur =
            nextCur < diagCount ? nextCur : diagCount > 0 ? diagCount - 1 : 0;
          return {
            ...state,
            sui: {
              ...sui,
              diagCursor: nextCur,
              diagScroll: adjustScroll(listCur, sui.diagScroll, 4),
            },
          };
        }
        if (key === 'DOWN') {
          const nextCur = (sui.diagCursor + 1) % (maxIdx + 1);
          const listCur =
            nextCur < diagCount ? nextCur : diagCount > 0 ? diagCount - 1 : 0;
          return {
            ...state,
            sui: {
              ...sui,
              diagCursor: nextCur,
              diagScroll: adjustScroll(listCur, sui.diagScroll, 4),
            },
          };
        }
        if (key === 'EXE') {
          if (sui.diagCursor >= 0 && sui.diagCursor < diagCount) {
            return { ...state, sui: { ...sui, diagDetailOpen: true } };
          }
          return {
            ...state,
            sui: {
              ...sui,
              diagConfirmModal: true,
              diagConfirmYes: false,
            },
          };
        }
      } else {
        if (key === 'UP' && sui.diagScroll > 0) {
          return { ...state, sui: { ...sui, diagScroll: sui.diagScroll - 1 } };
        }
        if (key === 'DOWN' && sui.diagScroll < 6) {
          return { ...state, sui: { ...sui, diagScroll: sui.diagScroll + 1 } };
        }
      }
      return state;
    }

    case 'SETTINGS_AUTOSTART': {
      return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
    }

    case 'SETTINGS_DISPLAY': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'UP') {
        const cur =
          sui.displayCursor > 0 ? sui.displayCursor - 1 : NL_DISPLAY_OPT_COUNT - 1;
        return {
          ...state,
          sui: {
            ...sui,
            displayCursor: cur,
            displayScroll: adjustScroll(cur, sui.displayScroll, 3),
          },
        };
      }
      if (key === 'DOWN') {
        const cur = (sui.displayCursor + 1) % NL_DISPLAY_OPT_COUNT;
        return {
          ...state,
          sui: {
            ...sui,
            displayCursor: cur,
            displayScroll: adjustScroll(cur, sui.displayScroll, 3),
          },
        };
      }
      if (key === 'LEFT' || key === 'RIGHT' || key === 'EXE') {
        if (sui.displayCursor === 0) {
          const curIdx = LAYOUT_ORDER.indexOf(state.settings.layout);
          const delta = key === 'LEFT' ? LAYOUT_ORDER.length - 1 : 1;
          const nextLayout = LAYOUT_ORDER[(curIdx + delta) % LAYOUT_ORDER.length];
          return snapshotActiveProfile(
            rebuildFilter({
              ...state,
              settings: { ...state.settings, layout: nextLayout },
            })
          );
        }
        if (sui.displayCursor === 1) {
          const nextDir: ScrollDir =
            (state.settings.scrollDir ?? 'vertical') === 'horizontal'
              ? 'vertical'
              : 'horizontal';
          return snapshotActiveProfile(
            rebuildFilter({
              ...state,
              settings: { ...state.settings, scrollDir: nextDir },
              toastMessage: `Defilement : ${SCROLL_DIR_LABELS[nextDir]}`,
              toastSeq: state.toastSeq + 1,
            })
          );
        }
        if (sui.displayCursor === 2) {
          return snapshotActiveProfile(
            rebuildFilter({
              ...state,
              settings: { ...state.settings, showNames: !state.settings.showNames },
            })
          );
        }
        if (sui.displayCursor === 3) {
          return snapshotActiveProfile(
            rebuildFilter({
              ...state,
              settings: { ...state.settings, showShortcuts: !state.settings.showShortcuts },
            })
          );
        }
        if (sui.displayCursor === 4 && key === 'EXE') {
          return requestCalculatorPoweroff(state);
        }
      }
      return state;
    }

    case 'SETTINGS_ANIMATIONS': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'UP') {
        const cur = sui.animCursor > 0 ? sui.animCursor - 1 : NL_ANIM_CAT_COUNT - 1;
        return { ...state, sui: { ...sui, animCursor: cur } };
      }
      if (key === 'DOWN') {
        const cur = (sui.animCursor + 1) % NL_ANIM_CAT_COUNT;
        return { ...state, sui: { ...sui, animCursor: cur } };
      }
      if (key === 'LEFT' || key === 'RIGHT' || key === 'EXE') {
        const dir = key === 'LEFT' ? -1 : 1;
        const row = Math.max(0, Math.min(NL_ANIM_CAT_COUNT - 1, sui.animCursor));
        if (row === 0) {
          const curIdx = APP_OPEN_ANIM_ORDER.indexOf(clampAppOpenAnimMode(state.settings.appOpenAnim));
          const nextIdx = (curIdx + dir + NL_OPEN_ANIM_COUNT) % NL_OPEN_ANIM_COUNT;
          const nextMode = APP_OPEN_ANIM_ORDER[nextIdx];
          return snapshotActiveProfile({
            ...state,
            settings: { ...state.settings, appOpenAnim: nextMode },
            toastMessage: `Ouverture : ${APP_OPEN_ANIM_LABELS[nextMode]}`,
            toastSeq: state.toastSeq + 1,
          });
        }
        if (row === 1) {
          const curIdx = APP_CLOSE_ANIM_ORDER.indexOf(clampAppCloseAnimMode(state.settings.appCloseAnim));
          const nextIdx = (curIdx + dir + NL_CLOSE_ANIM_COUNT) % NL_CLOSE_ANIM_COUNT;
          const nextMode = APP_CLOSE_ANIM_ORDER[nextIdx];
          return snapshotActiveProfile({
            ...state,
            settings: { ...state.settings, appCloseAnim: nextMode },
            toastMessage: `Fermeture : ${APP_CLOSE_ANIM_LABELS[nextMode]}`,
            toastSeq: state.toastSeq + 1,
          });
        }
        if (row === 2) {
          const curIdx = LAUNCH_ANIM_ORDER.indexOf(clampLaunchAnimMode(state.settings.launchAnimMode));
          const nextIdx = (curIdx + dir + NL_LAUNCH_ANIM_COUNT) % NL_LAUNCH_ANIM_COUNT;
          const nextMode = LAUNCH_ANIM_ORDER[nextIdx];
          return snapshotActiveProfile({
            ...state,
            settings: { ...state.settings, launchAnimMode: nextMode },
            toastMessage: `Lancement : ${LAUNCH_ANIM_LABELS[nextMode]}`,
            toastSeq: state.toastSeq + 1,
          });
        }
        const curIdx = RETURN_ANIM_ORDER.indexOf(clampReturnAnimMode(state.settings.returnAnimMode));
        const nextIdx = (curIdx + dir + NL_RETURN_ANIM_COUNT) % NL_RETURN_ANIM_COUNT;
        const nextMode = RETURN_ANIM_ORDER[nextIdx];
        return snapshotActiveProfile({
          ...state,
          settings: { ...state.settings, returnAnimMode: nextMode },
          toastMessage: `Retour : ${RETURN_ANIM_LABELS[nextMode]}`,
          toastSeq: state.toastSeq + 1,
        });
      }
      return state;
    }

    case 'SETTINGS_TOUCHPAD': {
      if (key === 'ESC') {
        if (sui.tpTab === 1) {
          return { ...state, sui: { ...sui, tpTab: 0 } };
        }
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'TAB') {
        const nextTab: 0 | 1 = sui.tpTab === 0 ? 1 : 0;
        return {
          ...state,
          sui: {
            ...sui,
            tpTab: nextTab,
            tpCustomRow: 0,
            tpStyleCursor: clampCursorStyleToCard(state.settings.touchpadCursorStyle),
            tpSizeCursor: cursorSizeToIndex(state.settings.cursorSize),
          },
        };
      }
      if (sui.tpTab === 1) {
        if (key === 'UP' || key === 'DOWN') {
          const nextRow: 0 | 1 = sui.tpCustomRow === 0 ? 1 : 0;
          return { ...state, sui: { ...sui, tpCustomRow: nextRow } };
        }
        if (key === 'LEFT' || key === 'RIGHT') {
          const delta = key === 'RIGHT' ? 1 : 2;
          if (sui.tpCustomRow === 0) {
            const nextStyleIdx = (sui.tpStyleCursor + delta) % 3;
            const chosen = cardIndexToCursorStyle(nextStyleIdx);
            return {
              ...state,
              settings: { ...state.settings, touchpadCursorStyle: chosen },
              sui: { ...sui, tpStyleCursor: nextStyleIdx },
              toastMessage: `Style : ${CURSOR_STYLE_LABELS[chosen]}`,
              toastSeq: state.toastSeq + 1,
            };
          } else {
            const nextSizeIdx = (sui.tpSizeCursor + delta) % 3;
            const chosenSize = indexToCursorSize(nextSizeIdx);
            return {
              ...state,
              settings: { ...state.settings, cursorSize: chosenSize },
              sui: { ...sui, tpSizeCursor: nextSizeIdx },
              toastMessage: `Taille curseur : ${CURSOR_SIZE_LABELS[chosenSize]}`,
              toastSeq: state.toastSeq + 1,
            };
          }
        }
        if (key === 'EXE') {
          if (sui.tpCustomRow === 0) {
            const chosen = cardIndexToCursorStyle(sui.tpStyleCursor);
            return {
              ...state,
              settings: { ...state.settings, touchpadCursorStyle: chosen },
              toastMessage: `Curseur : ${CURSOR_STYLE_LABELS[chosen]}`,
              toastSeq: state.toastSeq + 1,
            };
          } else {
            const chosenSize = indexToCursorSize(sui.tpSizeCursor);
            return {
              ...state,
              settings: { ...state.settings, cursorSize: chosenSize },
              toastMessage: `Taille curseur : ${CURSOR_SIZE_LABELS[chosenSize]}`,
              toastSeq: state.toastSeq + 1,
            };
          }
        }
        return state;
      }

      if (key === 'UP') {
        const nextCursor = (sui.tpCursor + NL_TP_TAB0_ROW_COUNT - 1) % NL_TP_TAB0_ROW_COUNT;
        const nextScroll = Math.min(
          NL_TP_TAB0_ROW_COUNT - NL_TP_TAB0_VISIBLE_ROWS,
          adjustScroll(nextCursor, sui.tpScroll, NL_TP_TAB0_VISIBLE_ROWS)
        );
        return { ...state, sui: { ...sui, tpCursor: nextCursor, tpScroll: nextScroll } };
      }
      if (key === 'DOWN') {
        const nextCursor = (sui.tpCursor + 1) % NL_TP_TAB0_ROW_COUNT;
        const nextScroll = Math.min(
          NL_TP_TAB0_ROW_COUNT - NL_TP_TAB0_VISIBLE_ROWS,
          adjustScroll(nextCursor, sui.tpScroll, NL_TP_TAB0_VISIBLE_ROWS)
        );
        return { ...state, sui: { ...sui, tpCursor: nextCursor, tpScroll: nextScroll } };
      }
      if (sui.tpCursor === 0) {
        if (key === 'EXE' || key === 'LEFT' || key === 'RIGHT') {
          const nextMode: TouchpadMode =
            state.settings.touchpadMode === 'mouse' ? 'touchpad' : 'mouse';
          const isMouse = nextMode === 'mouse';
          const nextStyle =
            isMouse && state.settings.touchpadCursorStyle === 'none'
              ? 'arrow'
              : state.settings.touchpadCursorStyle;
          return updateMouseHover({
            ...state,
            settings: {
              ...state.settings,
              touchpadMode: nextMode,
              touchpadCursorShow: isMouse,
              touchpadCursorStyle: nextStyle,
            },
            toastMessage: isMouse ? 'Mode Souris : ACTIF' : 'Mode Pave tactile : ACTIF',
            toastSeq: state.toastSeq + 1,
          });
        }
      } else if (sui.tpCursor === 1) {
        if (key === 'EXE' || key === 'LEFT' || key === 'RIGHT') {
          const nextTp = !state.settings.touchpadNav;
          return {
            ...state,
            settings: { ...state.settings, touchpadNav: nextTp },
            toastMessage: nextTp ? 'Glissement au pave : ON' : 'Glissement au pave : OFF',
            toastSeq: state.toastSeq + 1,
          };
        }
      } else if (sui.tpCursor === 2) {
        if (key === 'LEFT' || key === 'RIGHT') {
          const prevSens = clampTouchpadSensitivity(state.settings.touchpadSensitivity);
          const nextSens = clampTouchpadSensitivity(prevSens + (key === 'RIGHT' ? 1 : -1));
          if (nextSens === prevSens) return state;
          return {
            ...state,
            settings: { ...state.settings, touchpadSensitivity: nextSens },
            toastMessage: `Sensibilite pave : ${nextSens + 1}/5`,
            toastSeq: state.toastSeq + 1,
          };
        }
      } else if (sui.tpCursor === 3) {
        if (key === 'LEFT' || key === 'RIGHT') {
          const prevSpd = clampCursorSpeed(state.settings.cursorSpeed);
          const nextSpd = clampCursorSpeed(prevSpd + (key === 'RIGHT' ? 1 : -1));
          if (nextSpd === prevSpd) return state;
          return {
            ...state,
            settings: { ...state.settings, cursorSpeed: nextSpd },
            toastMessage: `Vitesse souris : ${nextSpd + 1}/5`,
            toastSeq: state.toastSeq + 1,
          };
        }
      } else if (sui.tpCursor === 4) {
        if (key === 'EXE' || key === 'LEFT' || key === 'RIGHT') {
          const nextTap = !state.settings.tapToClick;
          return {
            ...state,
            settings: { ...state.settings, tapToClick: nextTap },
            toastMessage: nextTap ? 'Tapotement pour cliquer : OUI' : 'Tapotement pour cliquer : NON',
            toastSeq: state.toastSeq + 1,
          };
        }
      } else if (sui.tpCursor === 5) {
        if (key === 'EXE' || key === 'LEFT' || key === 'RIGHT') {
          const curIdx = cursorSizeToIndex(state.settings.cursorSize);
          const delta = key === 'LEFT' ? 2 : 1;
          const nextIdx = (curIdx + delta) % 3;
          const nextSize = indexToCursorSize(nextIdx);
          return {
            ...state,
            settings: { ...state.settings, cursorSize: nextSize },
            sui: { ...sui, tpSizeCursor: nextIdx },
            toastMessage: `Taille curseur : ${CURSOR_SIZE_LABELS[nextSize]}`,
            toastSeq: state.toastSeq + 1,
          };
        }
      } else if (sui.tpCursor === 6) {
        if (key === 'EXE' || key === 'RIGHT') {
          return {
            ...state,
            sui: {
              ...sui,
              tpTab: 1,
              tpCustomRow: 0,
              tpStyleCursor: clampCursorStyleToCard(state.settings.touchpadCursorStyle),
              tpSizeCursor: cursorSizeToIndex(state.settings.cursorSize),
            },
          };
        }
      }
      return state;
    }

    case 'SETTINGS_SHORTCUTS': {
      if (sui.scConflictPrompt) {
        if (key === 'ESC') {
          return { ...state, sui: { ...sui, scConflictPrompt: false } };
        }
        if (key === 'LEFT' || key === 'RIGHT' || key === 'UP' || key === 'DOWN') {
          return {
            ...state,
            sui: { ...sui, scConflictConfirmYes: !sui.scConflictConfirmYes },
          };
        }
        if (key === 'EXE') {
          if (sui.scConflictConfirmYes) {
            return assignShortcutKeyToGame(state, sui.scGameCursor, sui.scConflictKey, true);
          }
          return { ...state, sui: { ...sui, scConflictPrompt: false } };
        }
        return state;
      }

      if (sui.scPickingKey) {
        const totalOpts = ASSIGNABLE_SHORTCUT_KEYS.length + 1;
        if (key === 'ESC') {
          return { ...state, sui: { ...sui, scPickingKey: false } };
        }
        if (key === 'LEFT' || key === 'UP') {
          const cur = sui.scKeyCursor > 0 ? sui.scKeyCursor - 1 : totalOpts - 1;
          return { ...state, sui: { ...sui, scKeyCursor: cur } };
        }
        if (key === 'RIGHT' || key === 'DOWN') {
          const cur = (sui.scKeyCursor + 1) % totalOpts;
          return { ...state, sui: { ...sui, scKeyCursor: cur } };
        }
        if (key === 'BACKSPACE') {
          const updatedGames = state.games.map((g, idx) =>
            idx === sui.scGameCursor ? { ...g, shortcutKey: '' } : g
          );
          return snapshotActiveProfile({
            ...state,
            games: updatedGames,
            sui: { ...sui, scPickingKey: false },
            toastMessage: 'RACCOURCI SUPPRIME',
            toastSeq: state.toastSeq + 1,
          });
        }
        if (key === 'EXE') {
          if (sui.scKeyCursor === 0) {
            const updatedGames = state.games.map((g, idx) =>
              idx === sui.scGameCursor ? { ...g, shortcutKey: '' } : g
            );
            return snapshotActiveProfile({
              ...state,
              games: updatedGames,
              sui: { ...sui, scPickingKey: false },
              toastMessage: 'RACCOURCI SUPPRIME',
              toastSeq: state.toastSeq + 1,
            });
          }
          const chosen = ASSIGNABLE_SHORTCUT_KEYS[sui.scKeyCursor - 1];
          return assignShortcutKeyToGame(state, sui.scGameCursor, chosen, false);
        }
        return state;
      }

      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (state.games.length === 0) return state;
      if (key === 'UP') {
        const cur = sui.scGameCursor > 0 ? sui.scGameCursor - 1 : state.games.length - 1;
        return {
          ...state,
          sui: { ...sui, scGameCursor: cur, scGameScroll: adjustScroll(cur, sui.scGameScroll, 6) },
        };
      }
      if (key === 'DOWN') {
        const cur = (sui.scGameCursor + 1) % state.games.length;
        return {
          ...state,
          sui: { ...sui, scGameCursor: cur, scGameScroll: adjustScroll(cur, sui.scGameScroll, 6) },
        };
      }
      if (key === 'BACKSPACE' || key === 'RESCAN') {
        if (state.games[sui.scGameCursor]?.shortcutKey) {
          const updatedGames = state.games.map((g, idx) =>
            idx === sui.scGameCursor ? { ...g, shortcutKey: '' } : g
          );
          return snapshotActiveProfile({
            ...state,
            games: updatedGames,
            toastMessage: 'RACCOURCI SUPPRIME',
            toastSeq: state.toastSeq + 1,
          });
        }
        return state;
      }
      if (key === 'EXE') {
        const curKey = state.games[sui.scGameCursor]?.shortcutKey ?? '';
        const kIdx = curKey ? ASSIGNABLE_SHORTCUT_KEYS.indexOf(curKey.toUpperCase()) : -1;
        return {
          ...state,
          sui: {
            ...sui,
            scPickingKey: true,
            scKeyCursor: kIdx >= 0 ? kIdx + 1 : 1,
          },
        };
      }
      return state;
    }

    case 'SETTINGS_KEY_HELP': {
      const defs = getShortcutDefs(state);
      const maxScroll = Math.max(0, defs.length - 7);
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'UP' && sui.subScroll > 0) {
        return { ...state, sui: { ...sui, subScroll: sui.subScroll - 1 } };
      }
      if (key === 'DOWN' && sui.subScroll < maxScroll) {
        return { ...state, sui: { ...sui, subScroll: sui.subScroll + 1 } };
      }
      return state;
    }

    case 'SETTINGS_SORT': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'UP') {
        const cur = sui.subCursor > 0 ? sui.subCursor - 1 : SORT_ORDER_LIST.length - 1;
        return {
          ...state,
          sui: { ...sui, subCursor: cur, subScroll: adjustScroll(cur, sui.subScroll, 6) },
        };
      }
      if (key === 'DOWN') {
        const cur = (sui.subCursor + 1) % SORT_ORDER_LIST.length;
        return {
          ...state,
          sui: { ...sui, subCursor: cur, subScroll: adjustScroll(cur, sui.subScroll, 6) },
        };
      }
      if (key === 'EXE') {
        const chosen = SORT_ORDER_LIST[sui.subCursor] ?? 'alpha_asc';
        return snapshotActiveProfile(
          rebuildFilter({
            ...state,
            selectedPos: 0,
            settings: { ...state.settings, sortOrder: chosen },
            toastMessage: SORT_ORDER_LABELS[chosen],
            toastSeq: state.toastSeq + 1,
          })
        );
      }
      return state;
    }

    case 'SETTINGS_STATS': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (state.games.length === 0) return state;
      if (key === 'UP') {
        const cur = sui.subCursor > 0 ? sui.subCursor - 1 : state.games.length - 1;
        return {
          ...state,
          sui: { ...sui, subCursor: cur, subScroll: adjustScroll(cur, sui.subScroll, 5) },
        };
      }
      if (key === 'DOWN') {
        const cur = (sui.subCursor + 1) % state.games.length;
        return {
          ...state,
          sui: { ...sui, subCursor: cur, subScroll: adjustScroll(cur, sui.subScroll, 5) },
        };
      }
      return state;
    }

    case 'SETTINGS_CAPTURE_BATCH': {
      if (key === 'ESC') {
        if (sui.capActive) {
          return cancelBatchCapture(state);
        }
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if ((key === 'RIGHT' || key === 'DOWN') && sui.capActive && sui.capPos < sui.capTotal) {
        return advanceBatchCapture(state, false, true, false);
      }
      if (key === 'EXE') {
        if (!sui.capActive) {
          return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
        }
        if (sui.capSemiAuto) {
          return { ...state, sui: { ...sui, capSemiAuto: false } };
        }
      }
      return state;
    }

    case 'SETTINGS_ICONS': {
      if (sui.iconConfirmModal) {
        if (key === 'ESC') {
          return {
            ...state,
            sui: { ...sui, iconConfirmModal: false, iconConfirmYes: false },
          };
        }
        if (key === 'LEFT' || key === 'RIGHT' || key === 'UP' || key === 'DOWN') {
          return {
            ...state,
            sui: { ...sui, iconConfirmYes: !sui.iconConfirmYes },
          };
        }
        if (key === 'EXE') {
          if (sui.iconConfirmYes && sui.iconCursor >= 0 && sui.iconCursor < state.games.length) {
            return deleteCustomIcon(state, sui.iconCursor);
          }
          return {
            ...state,
            sui: { ...sui, iconConfirmModal: false, iconConfirmYes: false },
          };
        }
        return state;
      }

      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      const total = state.games.length;
      if (total <= 0) return state;

      const cols = 3;
      const visRows = 2;
      let cur = Math.max(0, Math.min(total - 1, sui.iconCursor));

      if (key === 'LEFT') {
        cur = cur > 0 ? cur - 1 : total - 1;
      } else if (key === 'RIGHT') {
        cur = (cur + 1) % total;
      } else if (key === 'UP') {
        if (cur >= cols) cur -= cols;
      } else if (key === 'DOWN') {
        if (cur + cols < total) {
          cur += cols;
        } else {
          const rowCur = Math.floor(cur / cols);
          const lastRow = Math.floor((total - 1) / cols);
          if (rowCur < lastRow) cur = total - 1;
        }
      } else if (key === 'EXE' || key === 'RESCAN' || key === 'BACKSPACE') {
        const g = state.games[cur];
        if (g.iconSource !== 'NONE' && g.capturedIconDataUrl) {
          return {
            ...state,
            sui: {
              ...sui,
              iconCursor: cur,
              iconConfirmModal: true,
              iconConfirmYes: false,
            },
          };
        }
        return {
          ...state,
          toastMessage: 'ICONE PAR DEFAUT ACTIVE',
          toastSeq: state.toastSeq + 1,
        };
      }

      const row = Math.floor(cur / cols);
      return {
        ...state,
        sui: {
          ...sui,
          iconCursor: cur,
          iconScrollRow: adjustScroll(row, sui.iconScrollRow, visRows),
        },
      };
    }

    case 'SETTINGS_PROFILES': {
      const pcount = state.profiles.items.length;
      switch (sui.profMode) {
        case 'LIST': {
          const totalRows = pcount + 2;
          if (key === 'ESC') {
            return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
          }
          if (key === 'UP') {
            const cur = sui.profCursor > 0 ? sui.profCursor - 1 : totalRows - 1;
            return {
              ...state,
              sui: { ...sui, profCursor: cur, profScroll: adjustScroll(cur, sui.profScroll, 6) },
            };
          }
          if (key === 'DOWN') {
            const cur = (sui.profCursor + 1) % totalRows;
            return {
              ...state,
              sui: { ...sui, profCursor: cur, profScroll: adjustScroll(cur, sui.profScroll, 6) },
            };
          }
          if (key === 'LEFT' && sui.profCursor === 0) {
            const nextLockEn = !Boolean(state.settings.profileLockEnabled);
            return {
              ...state,
              settings: { ...state.settings, profileLockEnabled: nextLockEn },
              toastMessage: nextLockEn ? 'Cadenas profils : ACTIF' : 'Cadenas profils : DESACTIVE',
              toastSeq: state.toastSeq + 1,
            };
          }
          if (key === 'EXE' || key === 'RIGHT') {
            if (sui.profCursor === 0) {
              const nextLockEn = !Boolean(state.settings.profileLockEnabled);
              return {
                ...state,
                settings: { ...state.settings, profileLockEnabled: nextLockEn },
                toastMessage: nextLockEn
                  ? 'Cadenas profils : ACTIF'
                  : 'Cadenas profils : DESACTIVE',
                toastSeq: state.toastSeq + 1,
              };
            }
            if (sui.profCursor === 1) {
              const freeId = firstFreeAvatar(state);
              if (pcount >= NL_MAX_PROFILES || freeId < 0) {
                return {
                  ...state,
                  toastMessage: '43/43 icones deja utilisees',
                  toastSeq: state.toastSeq + 1,
                };
              }
              const cat = getAvatarCategoryOf(freeId);
              const { start } = getAvatarCategoryRange(cat);
              return {
                ...state,
                profPlusSeq: (state.profPlusSeq ?? 0) + 1,
                profPlusOriginX: 25,
                profPlusOriginY: 78,
                sui: {
                  ...sui,
                  profMode: 'CREATE_AVATAR',
                  profEditName: '',
                  profAvatarCat: cat,
                  profAvatarCursor: freeId - start,
                  profFromQuick: false,
                },
              };
            }
            const target = sui.profCursor - 2;
            if (target >= 0 && target < pcount) {
              return {
                ...state,
                sui: {
                  ...sui,
                  profDetailIdx: target,
                  profDetailCursor: 0,
                  profMode: 'DETAIL',
                },
              };
            }
          }
          return state;
        }

        case 'DETAIL': {
          if (sui.profDetailIdx < 0 || sui.profDetailIdx >= pcount) {
            return { ...state, sui: { ...sui, profMode: 'LIST' } };
          }
          const dp = state.profiles.items[sui.profDetailIdx];
          const lockEn = Boolean(state.settings.profileLockEnabled);
          const detailRows = lockEn ? 5 : 4;
          const isProt = isProfileProtected(state, sui.profDetailIdx);
          if (key === 'ESC' || key === 'LEFT') {
            return { ...state, sui: { ...sui, profMode: 'LIST' } };
          }
          if (key === 'UP') {
            const cur = sui.profDetailCursor > 0 ? sui.profDetailCursor - 1 : detailRows - 1;
            return { ...state, sui: { ...sui, profDetailCursor: cur } };
          }
          if (key === 'DOWN') {
            const cur = (sui.profDetailCursor + 1) % detailRows;
            return { ...state, sui: { ...sui, profDetailCursor: cur } };
          }
          if (key === 'EXE' || key === 'RIGHT') {
            if (sui.profDetailCursor === 0) {
              if (isProt) {
                return {
                  ...state,
                  toastMessage: 'Profil verrouille',
                  toastSeq: state.toastSeq + 1,
                };
              }
              return {
                ...state,
                sui: {
                  ...sui,
                  profEditName: dp.name,
                  profMode: 'EDIT_NAME',
                },
              };
            }
            if (sui.profDetailCursor === 1) {
              if (isProt) {
                return {
                  ...state,
                  toastMessage: 'Profil verrouille',
                  toastSeq: state.toastSeq + 1,
                };
              }
              const cat = getAvatarCategoryOf(dp.avatarId);
              const { start, count } = getAvatarCategoryRange(cat);
              let avCur = dp.avatarId - start;
              if (avCur < 0 || avCur >= count) avCur = 0;
              return {
                ...state,
                sui: {
                  ...sui,
                  profAvatarCat: cat,
                  profAvatarCursor: avCur,
                  profMode: 'EDIT_AVATAR',
                },
              };
            }
            if (sui.profDetailCursor === 2) {
              if (sui.profDetailIdx === state.profiles.activeIndex) {
                return {
                  ...state,
                  toastMessage: 'Profil deja actif',
                  toastSeq: state.toastSeq + 1,
                };
              }
              return switchProfileTo(state, sui.profDetailIdx);
            }
            if (lockEn && sui.profDetailCursor === 3) {
              return toggleProfileLock(state, sui.profDetailIdx).state;
            }
            const delRow = lockEn ? 4 : 3;
            if (sui.profDetailCursor === delRow) {
              if (isProt) {
                return {
                  ...state,
                  toastMessage: 'Profil verrouille',
                  toastSeq: state.toastSeq + 1,
                };
              }
              if (dp.isPrimary || pcount <= 1) {
                return {
                  ...state,
                  toastMessage: 'Profil principal protege',
                  toastSeq: state.toastSeq + 1,
                };
              }
              return {
                ...state,
                sui: {
                  ...sui,
                  profDeleteYes: false,
                  profMode: 'CONFIRM_DELETE',
                },
              };
            }
          }
          return state;
        }

        case 'EDIT_NAME':
        case 'CREATE_NAME': {
          const isCreate = sui.profMode === 'CREATE_NAME';
          if (key === 'ESC') {
            if (isCreate) {
              if (sui.profFromQuick) {
                return {
                  ...state,
                  sui: {
                    ...sui,
                    profFromQuick: false,
                    profMode: 'LIST',
                    screen: 'LIBRARY',
                  },
                };
              }
              return { ...state, sui: { ...sui, profMode: 'LIST' } };
            }
            return { ...state, sui: { ...sui, profMode: 'DETAIL' } };
          }
          if (key === 'BACKSPACE' || key === 'RESCAN') {
            return {
              ...state,
              sui: {
                ...sui,
                profEditName: sui.profEditName.slice(0, -1),
              },
            };
          }
          if (key === 'SPACE') {
            if (sui.profEditName.length < NL_PROFILE_NAME_MAX) {
              return {
                ...state,
                sui: {
                  ...sui,
                  profEditName: sui.profEditName + ' ',
                },
              };
            }
            return state;
          }
          if (key === 'EXE') {
            if (!sui.profEditName.trim()) {
              return {
                ...state,
                toastMessage: 'Entrez un nom valide',
                toastSeq: state.toastSeq + 1,
              };
            }
            if (isCreate) {
              const freeId = firstFreeAvatar(state);
              if (freeId < 0) {
                return {
                  ...state,
                  toastMessage: '43/43 icones deja utilisees',
                  toastSeq: state.toastSeq + 1,
                };
              }
              const cat = getAvatarCategoryOf(freeId);
              const { start } = getAvatarCategoryRange(cat);
              return {
                ...state,
                sui: {
                  ...sui,
                  profAvatarCat: cat,
                  profAvatarCursor: freeId - start,
                  profMode: 'CREATE_AVATAR',
                },
              };
            }
            const renamed = renameUserProfile(state, sui.profDetailIdx, sui.profEditName);
            if (renamed.ok) {
              return {
                ...renamed.state,
                sui: { ...renamed.state.sui, profMode: 'DETAIL' },
                toastMessage: 'Nom mis a jour',
                toastSeq: renamed.state.toastSeq + 1,
              };
            }
            return {
              ...state,
              toastMessage: 'Nom invalide',
              toastSeq: state.toastSeq + 1,
            };
          }
          return state;
        }

        case 'EDIT_AVATAR':
        case 'CREATE_AVATAR': {
          const isCreate = sui.profMode === 'CREATE_AVATAR';
          const cat = sui.profAvatarCat >= 0 && sui.profAvatarCat < NL_AVATAR_CAT_COUNT
            ? sui.profAvatarCat
            : 0;
          const { start: cstart, count: ccnt } = getAvatarCategoryRange(cat);
          const cols = cat === 0 ? 8 : 5;

          if (key === 'ESC') {
            if (isCreate) {
              if (sui.profFromQuick) {
                return {
                  ...state,
                  sui: {
                    ...sui,
                    profFromQuick: false,
                    profMode: 'LIST',
                    screen: 'LIBRARY',
                    qpOpen: true,
                    qpCursor: state.profiles.activeIndex,
                  },
                };
              }
              return {
                ...state,
                sui: {
                  ...sui,
                  profMode: 'LIST',
                },
              };
            }
            return {
              ...state,
              sui: {
                ...sui,
                profMode: 'DETAIL',
              },
            };
          }
          if (key === 'TAB' || key === 'PAGE_NEXT') {
            return {
              ...state,
              sui: {
                ...sui,
                profAvatarCat: (cat + 1) % NL_AVATAR_CAT_COUNT,
                profAvatarCursor: 0,
              },
            };
          }
          if (key === 'PAGE_PREV') {
            return {
              ...state,
              sui: {
                ...sui,
                profAvatarCat: (cat + NL_AVATAR_CAT_COUNT - 1) % NL_AVATAR_CAT_COUNT,
                profAvatarCursor: 0,
              },
            };
          }
          if (key === 'LEFT') {
            if (sui.profAvatarCursor > 0) {
              return { ...state, sui: { ...sui, profAvatarCursor: sui.profAvatarCursor - 1 } };
            }
            const nextCat = (cat + NL_AVATAR_CAT_COUNT - 1) % NL_AVATAR_CAT_COUNT;
            const { count: ncCnt } = getAvatarCategoryRange(nextCat);
            return {
              ...state,
              sui: { ...sui, profAvatarCat: nextCat, profAvatarCursor: Math.max(0, ncCnt - 1) },
            };
          }
          if (key === 'RIGHT') {
            if (sui.profAvatarCursor + 1 < ccnt) {
              return { ...state, sui: { ...sui, profAvatarCursor: sui.profAvatarCursor + 1 } };
            }
            return {
              ...state,
              sui: { ...sui, profAvatarCat: (cat + 1) % NL_AVATAR_CAT_COUNT, profAvatarCursor: 0 },
            };
          }
          if (key === 'UP') {
            if (sui.profAvatarCursor >= cols) {
              return { ...state, sui: { ...sui, profAvatarCursor: sui.profAvatarCursor - cols } };
            }
            return {
              ...state,
              sui: {
                ...sui,
                profAvatarCat: (cat + NL_AVATAR_CAT_COUNT - 1) % NL_AVATAR_CAT_COUNT,
                profAvatarCursor: 0,
              },
            };
          }
          if (key === 'DOWN') {
            if (sui.profAvatarCursor + cols < ccnt) {
              return { ...state, sui: { ...sui, profAvatarCursor: sui.profAvatarCursor + cols } };
            }
            if (Math.floor(sui.profAvatarCursor / cols) < Math.floor((ccnt - 1) / cols)) {
              return { ...state, sui: { ...sui, profAvatarCursor: ccnt - 1 } };
            }
            return {
              ...state,
              sui: { ...sui, profAvatarCat: (cat + 1) % NL_AVATAR_CAT_COUNT, profAvatarCursor: 0 },
            };
          }
          if (key === 'EXE') {
            const avatarId = cstart + sui.profAvatarCursor;
            const excl = isCreate ? -1 : sui.profDetailIdx;
            if (findAvatarOwner(state, avatarId, excl) >= 0) {
              return {
                ...state,
                toastMessage: 'Icone deja utilisee',
                toastSeq: state.toastSeq + 1,
              };
            }
            if (isCreate) {
              const res = createUserProfile(state, sui.profEditName, avatarId);
              if (res.createdIdx >= 0) {
                const newProf = res.state.profiles.items[res.createdIdx];
                if (sui.profFromQuick) {
                  return {
                    ...res.state,
                    profBirthSeq: (state.profBirthSeq ?? 0) + 1,
                    profBirthAvatarId: avatarId,
                    profBirthSlot: res.createdIdx,
                    sui: {
                      ...res.state.sui,
                      profFromQuick: false,
                      profMode: 'LIST',
                      screen: 'LIBRARY',
                      qpOpen: true,
                      qpCursor: res.createdIdx,
                    },
                    toastMessage: `Profil ${newProf.name} cree`,
                    toastSeq: res.state.toastSeq + 1,
                  };
                }
                const nextCur = res.createdIdx + 1;
                return {
                  ...res.state,
                  profBirthSeq: (state.profBirthSeq ?? 0) + 1,
                  profBirthAvatarId: avatarId,
                  profBirthSlot: res.createdIdx,
                  sui: {
                    ...res.state.sui,
                    profMode: 'LIST',
                    profCursor: nextCur,
                    profScroll: adjustScroll(nextCur, sui.profScroll, 6),
                  },
                  toastMessage: `Profil ${newProf.name} cree`,
                  toastSeq: res.state.toastSeq + 1,
                };
              }
              return {
                ...state,
                toastMessage: 'Impossible de creer',
                toastSeq: state.toastSeq + 1,
              };
            }
            const avRes = setUserProfileAvatar(state, sui.profDetailIdx, avatarId);
            if (avRes.ok) {
              return {
                ...avRes.state,
                sui: { ...avRes.state.sui, profMode: 'DETAIL' },
                toastMessage: 'Icone mise a jour',
                toastSeq: avRes.state.toastSeq + 1,
              };
            }
            return {
              ...state,
              toastMessage: 'Icone deja utilisee',
              toastSeq: state.toastSeq + 1,
            };
          }
          return state;
        }

        case 'CONFIRM_DELETE': {
          if (key === 'ESC') {
            return { ...state, sui: { ...sui, profMode: 'DETAIL' } };
          }
          if (key === 'LEFT' || key === 'RIGHT' || key === 'UP' || key === 'DOWN') {
            return { ...state, sui: { ...sui, profDeleteYes: !sui.profDeleteYes } };
          }
          if (key === 'EXE') {
            if (sui.profDeleteYes) {
              const delRes = deleteUserProfile(state, sui.profDetailIdx);
              if (delRes.ok) {
                const nextCur = delRes.state.profiles.activeIndex + 1;
                return {
                  ...delRes.state,
                  sui: {
                    ...delRes.state.sui,
                    profMode: 'LIST',
                    profCursor: nextCur,
                    profScroll: adjustScroll(nextCur, 0, 6),
                  },
                  toastMessage: 'Profil supprime',
                  toastSeq: delRes.state.toastSeq + 1,
                };
              }
              return {
                ...state,
                sui: { ...state.sui, profMode: 'DETAIL' },
                toastMessage: 'Suppression refusee',
                toastSeq: state.toastSeq + 1,
              };
            }
            return { ...state, sui: { ...sui, profMode: 'DETAIL' } };
          }
          return state;
        }
      }
      return state;
    }

    case 'SETTINGS_RESET': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'LEFT' || key === 'RIGHT' || key === 'UP' || key === 'DOWN') {
        return {
          ...state,
          sui: { ...sui, resetConfirmYes: !sui.resetConfirmYes },
        };
      }
      if (key === 'EXE') {
        if (sui.resetConfirmYes) {
          return resetSettingsOnly(state);
        }
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      return state;
    }

    case 'SETTINGS_RESCAN': {
      const rs = sui.rescan;
      if (rs.emptyConfirmPending) {
        if (key === 'LEFT' || key === 'RIGHT') {
          return {
            ...state,
            sui: {
              ...sui,
              rescan: { ...rs, emptyConfirmReplace: !rs.emptyConfirmReplace },
            },
          };
        }
        if (key === 'ESC') {
          return {
            ...state,
            sui: {
              ...sui,
              rescan: {
                ...rs,
                emptyConfirmPending: false,
                emptyConfirmReplace: false,
                finished: true,
              },
            },
            toastMessage: 'BIBLIOTHEQUE CONSERVEE',
            toastSeq: state.toastSeq + 1,
          };
        }
        if (key === 'EXE') {
          if (rs.emptyConfirmReplace) {
            return rebuildFilter({
              ...state,
              games: [],
              sui: {
                ...sui,
                rescan: {
                  ...rs,
                  gamesFound: 0,
                  gamesKept: 0,
                  gamesRemoved: state.games.length,
                  emptyConfirmPending: false,
                  emptyConfirmReplace: false,
                  finished: true,
                },
              },
              sys: {
                ...state.sys,
                cacheValid: false,
                cacheCrc32: 0,
              },
              toastMessage: 'BIBLIOTHEQUE VIDEE',
              toastSeq: state.toastSeq + 1,
            });
          }
          return {
            ...state,
            sui: {
              ...sui,
              rescan: {
                ...rs,
                emptyConfirmPending: false,
                emptyConfirmReplace: false,
                finished: true,
              },
            },
            toastMessage: 'BIBLIOTHEQUE CONSERVEE',
            toastSeq: state.toastSeq + 1,
          };
        }
        return state;
      }

      if (key === 'ESC' || key === 'EXE') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'BACKSPACE' || key === 'RESCAN') {
        return runSimulatorRescan(state);
      }
      return state;
    }

    case 'SETTINGS_HOTKEY': {
      if (key === 'ESC') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      if (key === 'UP' || key === 'DOWN') {
        return {
          ...state,
          sui: { ...sui, hotkeyCursor: sui.hotkeyCursor === 0 ? 1 : 0 },
        };
      }
      if (key === 'EXE' || key === 'LEFT' || key === 'RIGHT') {
        if (sui.hotkeyCursor === 1) {
          const curIdx = HOTKEY_COMBO_ORDER.indexOf(state.settings.hotkeyCombo ?? 'ctrl_menu');
          const delta = key === 'LEFT' ? -1 : 1;
          const nextCombo =
            HOTKEY_COMBO_ORDER[(curIdx + delta + HOTKEY_COMBO_ORDER.length) % HOTKEY_COMBO_ORDER.length];
          const lbl = HOTKEY_COMBO_LABELS[nextCombo];
          return {
            ...state,
            settings: { ...state.settings, hotkeyCombo: nextCombo },
            sys: state.settings.globalHotkey
              ? {
                  ...state.sys,
                  hotkeyDiag: `RAM + startup : ${lbl}`,
                }
              : state.sys,
            toastMessage: `Touche : ${lbl}`,
            toastSeq: state.toastSeq + 1,
          };
        }
        const target = !state.settings.globalHotkey;
        if (!state.sys.hasNdless) {
          return {
            ...state,
            settings: { ...state.settings, globalHotkey: false },
            sys: {
              ...state.sys,
              hotkeyActive: false,
              hotkeyRamActive: false,
              hotkeyDiag: 'Ndless requis pour le hook TI-OS',
            },
            toastMessage: 'Ndless requis',
            toastSeq: state.toastSeq + 1,
          };
        }
        const lbl = HOTKEY_COMBO_LABELS[state.settings.hotkeyCombo ?? 'ctrl_menu'];
        return {
          ...state,
          settings: { ...state.settings, globalHotkey: target },
          sys: {
            ...state.sys,
            hotkeyActive: target,
            hotkeyRamActive: target,
            hotkeyDiag: target
              ? `RAM + startup : ${lbl}`
              : 'Raccourci TI-OS desactive',
          },
          toastMessage: target ? `Raccourci (${lbl}): ON` : 'Raccourci TI-OS : OFF',
          toastSeq: state.toastSeq + 1,
        };
      }
      return state;
    }

    case 'SETTINGS_INFO':
    case 'SETTINGS_CREDITS': {
      if (key === 'ESC' || key === 'EXE') {
        return { ...state, sui: { ...sui, screen: 'SETTINGS_MAIN' } };
      }
      return state;
    }

    default:
      return state;
  }
}

export function handleLauncherKey(
  state: LauncherState,
  key: NavKey,
  nowSec = Math.floor(Date.now() / 1000),
  explicitPlayDeltaSec?: number
): LauncherState {
  if (state.runningGameIndex !== null) {
    if (key === 'ESC') {
      const gIdx = state.runningGameIndex;
      const computedDelta =
        explicitPlayDeltaSec !== undefined
          ? explicitPlayDeltaSec
          : state.runningStartMs !== null
          ? Math.max(1, Math.round((Date.now() - state.runningStartMs) / 1000))
          : 0;
      const clampedDelta =
        computedDelta >= 0 && computedDelta <= 43200 ? computedDelta : 0;
      const updatedGames =
        clampedDelta > 0
          ? state.games.map((g, idx) =>
              idx === gIdx ? { ...g, playTimeSec: g.playTimeSec + clampedDelta } : g
            )
          : state.games;
      /* Refresh library sort ONLY upon returning from the game (never on launch) */
      const baseReturned: LauncherState = {
        ...state,
        games: updatedGames,
        runningGameIndex: null,
        runningStartMs: null,
        lastExecStatus: 0,
      };
      const needsRebuild = returnNeedsFilterRebuild(baseReturned);
      const rebuilt = needsRebuild ? rebuildFilter(baseReturned) : baseReturned;
      const newSlot = needsRebuild ? rebuilt.filteredIndices.indexOf(gIdx) : state.selectedPos;
      const mAfter = getLayoutMetrics(
        rebuilt.settings.layout,
        rebuilt.settings.showNames,
        rebuilt.settings.showShortcuts
      );
      const emitReturnAnim = clampReturnAnimMode(rebuilt.settings.returnAnimMode) !== 'instant';
      const nextRetSeq = emitReturnAnim ? (state.returnDropSeq ?? 0) + 1 : (state.returnDropSeq ?? 0);
      const exitedState: LauncherState =
        newSlot >= 0
          ? {
              ...rebuilt,
              selectedPos: newSlot,
              currentPage: Math.floor(newSlot / mAfter.perPage),
              returnDropSeq: nextRetSeq,
              returnDropGameIdx: gIdx,
            }
          : {
              ...rebuilt,
              returnDropSeq: nextRetSeq,
              returnDropGameIdx: gIdx,
            };
      return snapshotActiveProfile(exitedState);
    }
    return state;
  }

  if (state.sui.screen !== 'LIBRARY') {
    return handleSettingsKey(state, key, nowSec);
  }

  /* Quick Profile Selector modal overlay on Library (STRICTLY browse/switch/'+' only — ZERO edit/delete) */
  if (state.sui.qpOpen) {
    const pcount = Math.max(1, state.profiles.items.length);
    const totalSlots = pcount + 1;
    switch (key) {
      case 'ESC':
      case 'PROFILE_QUICK':
        return {
          ...state,
          sui: { ...state.sui, qpOpen: false },
        };
      case 'LEFT': {
        const cur = state.sui.qpCursor > 0 ? state.sui.qpCursor - 1 : totalSlots - 1;
        return {
          ...state,
          sui: { ...state.sui, qpCursor: cur },
        };
      }
      case 'RIGHT': {
        const cur = (state.sui.qpCursor + 1) % totalSlots;
        return {
          ...state,
          sui: { ...state.sui, qpCursor: cur },
        };
      }
      case 'PROFILE_PREV': {
        const stepped = stepProfile(state, -1);
        if (stepped === state) return state;
        return {
          ...stepped,
          sui: { ...stepped.sui, qpCursor: stepped.profiles.activeIndex },
        };
      }
      case 'PROFILE_NEXT': {
        const stepped = stepProfile(state, +1);
        if (stepped === state) return state;
        return {
          ...stepped,
          sui: { ...stepped.sui, qpCursor: stepped.profiles.activeIndex },
        };
      }
      case 'EXE': {
        if (state.sui.qpCursor < pcount) {
          const targetIdx = state.sui.qpCursor;
          const origX = computeQuickProfileSlotCenterX(pcount, targetIdx);
          const switched =
            targetIdx !== state.profiles.activeIndex
              ? switchProfileTo(state, targetIdx, origX, 114, 36)
              : state;
          return {
            ...switched,
            sui: { ...switched.sui, qpOpen: false },
          };
        }
        const freeId = firstFreeAvatar(state);
        if (pcount >= NL_MAX_PROFILES || freeId < 0) {
          return {
            ...state,
            toastMessage: '43/43 icones deja utilisees',
            toastSeq: state.toastSeq + 1,
          };
        }
        const cat = getAvatarCategoryOf(freeId);
        const { start } = getAvatarCategoryRange(cat);
        const plusX = computeQuickProfileSlotCenterX(pcount, pcount);
        return {
          ...state,
          profPlusSeq: (state.profPlusSeq ?? 0) + 1,
          profPlusOriginX: plusX,
          profPlusOriginY: 114,
          sui: {
            ...state.sui,
            qpOpen: false,
            screen: 'SETTINGS_PROFILES',
            profMode: 'CREATE_AVATAR',
            profEditName: '',
            profAvatarCat: cat,
            profAvatarCursor: freeId - start,
            profFromQuick: true,
          },
        };
      }
      case 'DOCK': {
        const idx = DISPLAY_MODE_ORDER.indexOf(state.displayMode);
        return {
          ...state,
          displayMode: DISPLAY_MODE_ORDER[(idx + 1) % DISPLAY_MODE_ORDER.length],
        };
      }
      case 'MENU': {
        const idx = ACCENT_COLOR_ORDER.indexOf(state.accentColor);
        return {
          ...state,
          accentColor: ACCENT_COLOR_ORDER[(idx + 1) % ACCENT_COLOR_ORDER.length],
        };
      }
      default:
        return state;
    }
  }

  const count = state.filteredIndices.length;
  const m = getLayoutMetrics(
    state.settings.layout,
    state.settings.showNames,
    state.settings.showShortcuts
  );

  switch (key) {
    case 'PROFILE_QUICK':
      if (!state.sui.searchActive) {
        return {
          ...state,
          sui: {
            ...state.sui,
            qpOpen: true,
            qpCursor: state.profiles.activeIndex,
          },
        };
      }
      return state;

    case 'PROFILE_PREV':
      if (!state.sui.searchActive) {
        return stepProfile(state, -1);
      }
      return state;

    case 'PROFILE_NEXT':
      if (!state.sui.searchActive) {
        return stepProfile(state, +1);
      }
      return state;

    case 'SETTINGS':
      return {
        ...state,
        sui: {
          ...state.sui,
          screen: 'SETTINGS_MAIN',
          menuCursor: 0,
          menuScroll: 0,
        },
      };

    case 'LEFT': {
      if (count === 0) return state;
      const nextPos = state.selectedPos > 0 ? state.selectedPos - 1 : count - 1;
      return {
        ...state,
        selectedPos: nextPos,
        currentPage: Math.floor(nextPos / m.perPage),
        navSeq: state.navSeq + 1,
      };
    }
    case 'RIGHT': {
      if (count === 0) return state;
      const nextPos = state.selectedPos + 1 < count ? state.selectedPos + 1 : 0;
      return {
        ...state,
        selectedPos: nextPos,
        currentPage: Math.floor(nextPos / m.perPage),
        navSeq: state.navSeq + 1,
      };
    }
    case 'UP': {
      if (count === 0) return state;
      if (state.selectedPos >= m.cols) {
        const nextPos = state.selectedPos - m.cols;
        return {
          ...state,
          selectedPos: nextPos,
          currentPage: Math.floor(nextPos / m.perPage),
          navSeq: state.navSeq + 1,
        };
      }
      return state;
    }
    case 'DOWN': {
      if (count === 0) return state;
      let nextPos = state.selectedPos;
      if (state.selectedPos + m.cols < count) {
        nextPos = state.selectedPos + m.cols;
      } else {
        const rowCur = Math.floor(state.selectedPos / m.cols);
        const lastRow = Math.floor((count - 1) / m.cols);
        if (rowCur < lastRow) {
          nextPos = count - 1;
        } else {
          return state;
        }
      }
      return {
        ...state,
        selectedPos: nextPos,
        currentPage: Math.floor(nextPos / m.perPage),
        navSeq: state.navSeq + 1,
      };
    }
    case 'PAGE_PREV': {
      if (count === 0 || state.totalPages <= 1) return state;
      const slot = state.selectedPos % m.perPage;
      const newPage = state.currentPage > 0 ? state.currentPage - 1 : state.totalPages - 1;
      const newPos = Math.min(count - 1, newPage * m.perPage + slot);
      return {
        ...state,
        selectedPos: newPos,
        currentPage: newPage,
        navSeq: state.navSeq + 1,
      };
    }
    case 'PAGE_NEXT': {
      if (count === 0 || state.totalPages <= 1) return state;
      const slot = state.selectedPos % m.perPage;
      const newPage = state.currentPage + 1 < state.totalPages ? state.currentPage + 1 : 0;
      const newPos = Math.min(count - 1, newPage * m.perPage + slot);
      return {
        ...state,
        selectedPos: newPos,
        currentPage: newPage,
        navSeq: state.navSeq + 1,
      };
    }
    case 'TAB': {
      const idx = CATEGORY_ORDER.indexOf(state.activeCategory);
      const nextCat = CATEGORY_ORDER[(idx + 1) % CATEGORY_ORDER.length];
      return snapshotActiveProfile(
        rebuildFilter({
          ...state,
          activeCategory: nextCat,
          selectedPos: 0,
          navSeq: state.navSeq + 1,
        })
      );
    }
    case 'BACKSPACE': {
      if (state.sui.searchActive) {
        if (state.sui.searchQuery.length > 0) {
          return rebuildFilter({
            ...state,
            selectedPos: 0,
            navSeq: state.navSeq + 1,
            sui: {
              ...state.sui,
              searchQuery: state.sui.searchQuery.slice(0, -1),
            },
          });
        }
        return state;
      }
      return state;
    }
    case 'SPACE': {
      if (state.sui.searchActive && state.sui.searchQuery.length < 24) {
        return rebuildFilter({
          ...state,
          selectedPos: 0,
          navSeq: state.navSeq + 1,
          sui: {
            ...state.sui,
            searchQuery: state.sui.searchQuery + ' ',
          },
        });
      }
      return state;
    }
    case 'FAV': {
      if (count === 0) return state;
      if (isProfileProtected(state, state.profiles.activeIndex)) {
        return {
          ...state,
          toastMessage: 'Profil verrouille',
          toastSeq: state.toastSeq + 1,
        };
      }
      const gameIdx = state.filteredIndices[state.selectedPos];
      const nextFav = !state.games[gameIdx].isFavorite;
      const updatedGames = state.games.map((g, i) =>
        i === gameIdx ? { ...g, isFavorite: nextFav } : g
      );
      return snapshotActiveProfile(
        rebuildFilter({
          ...state,
          games: updatedGames,
          toastMessage: nextFav ? 'AJOUTE AUX FAVORIS' : 'RETIRE DES FAVORIS',
          toastSeq: state.toastSeq + 1,
        })
      );
    }
    case 'EXE': {
      if (count === 0) return state;
      const targetIdx = state.filteredIndices[state.selectedPos];
      return {
        ...triggerGameLaunch(state, targetIdx, nowSec),
        launchHolding: false,
      };
    }
    case 'ESC': {
      if (state.sui.searchActive) {
        return rebuildFilter({
          ...state,
          selectedPos: 0,
          navSeq: state.navSeq + 1,
          sui: {
            ...state.sui,
            searchActive: false,
            searchQuery: '',
          },
        });
      }
      if (state.activeCategory !== 'ALL') {
        return snapshotActiveProfile(
          rebuildFilter({
            ...state,
            activeCategory: 'ALL',
            selectedPos: 0,
            navSeq: state.navSeq + 1,
          })
        );
      }
      return state;
    }
    case 'DOCK': {
      const idx = DISPLAY_MODE_ORDER.indexOf(state.displayMode);
      return {
        ...state,
        displayMode: DISPLAY_MODE_ORDER[(idx + 1) % DISPLAY_MODE_ORDER.length],
      };
    }

    case 'MENU': {
      const idx = ACCENT_COLOR_ORDER.indexOf(state.accentColor);
      const nextColor = ACCENT_COLOR_ORDER[(idx + 1) % ACCENT_COLOR_ORDER.length];
      return {
        ...state,
        accentColor: nextColor,
      };
    }

    case 'RESCAN': {
      return rebuildFilter({
        ...state,
        toastMessage: 'BIBLIOTHEQUE ACTUALISEE',
        toastSeq: state.toastSeq + 1,
      });
    }

    default:
      return state;
  }
}

export function isMouseModeActive(state: LauncherState): boolean {
  return state.settings.touchpadMode === 'mouse' || Boolean(state.sui.ctrlMouseActive);
}

export function isTouchpadCursorVisible(state: LauncherState): boolean {
  return isMouseModeActive(state);
}

export function computeTouchpadCursorTarget(state: LauncherState): { x: number; y: number } {
  if (isMouseModeActive(state)) {
    return {
      x: Math.max(0, Math.min(NL_SCREEN_W - 1, Math.round(state.sui.mouseX))),
      y: Math.max(0, Math.min(NL_SCREEN_H - 1, Math.round(state.sui.mouseY))),
    };
  }

  let tx = 160;
  let ty = 120;

  if (state.sui.screen === 'LIBRARY') {
    if (state.sui.qpOpen) {
      const pcount = Math.max(1, state.profiles.items.length);
      const totalSlots = pcount + 1;
      const visMax = 6;
      const diam = 34;
      const stride = 44;
      const cur = Math.max(0, Math.min(totalSlots - 1, state.sui.qpCursor));
      const shown = Math.min(totalSlots, visMax);
      let winStart = 0;
      if (totalSlots > visMax) {
        winStart = Math.max(0, Math.min(totalSlots - visMax, cur - Math.trunc(visMax / 2)));
      }
      const stripW = shown * diam + (shown - 1) * (stride - diam);
      const startX = 160 - Math.trunc(stripW / 2);
      const rel = cur - winStart;
      tx = startX + rel * stride + Math.trunc(diam / 2);
      ty = 114;
    } else if (state.filteredIndices.length > 0) {
      const m = getLayoutMetrics(
        state.settings.layout,
        state.settings.showNames,
        state.settings.showShortcuts,
        state.filteredIndices.length,
        state.settings.scrollDir ?? 'vertical'
      );
      const pos = Math.max(0, Math.min(state.filteredIndices.length - 1, state.selectedPos));
      const page = Math.trunc(pos / m.perPage);
      const scrollX = m.isHorizontal ? page * m.pageStrideX : 0;
      const scrollY = m.isHorizontal ? 0 : page * m.pageStrideY;
      const searchH = state.sui.searchActive ? 28 : 0;
      const slot = computeGridSlotPos(m, state.filteredIndices.length, pos);
      const sx = slot.x - scrollX;
      const sy = slot.y + searchH - scrollY;
      tx = sx + Math.trunc(m.cardW / 2);
      ty = sy + Math.trunc(m.cardH / 2);
    }
  } else if (state.sui.screen === 'SETTINGS_MAIN') {
    const rel = Math.max(0, Math.min(5, state.sui.menuCursor - state.sui.menuScroll));
    tx = 160;
    ty = 32 + rel * 30 + 14;
  } else if (state.sui.screen === 'SETTINGS_TOUCHPAD') {
    if (state.sui.tpTab === 1) {
      if (state.sui.tpCustomRow === 1) {
        const s = Math.max(0, Math.min(2, state.sui.tpSizeCursor));
        tx = 59 + s * 101;
        ty = 178;
      } else {
        const c = Math.max(0, Math.min(2, state.sui.tpStyleCursor));
        tx = 59 + c * 101;
        ty = 92;
      }
    } else {
      const rects = getTouchpadTab0RowRects(state.sui.tpScroll);
      const hit = rects.find((r) => r.row === state.sui.tpCursor) ?? rects[0];
      if (hit) {
        ty = hit.y + Math.floor(hit.h / 2);
        if (hit.row === 2) {
          tx = 34 + clampTouchpadSensitivity(state.settings.touchpadSensitivity) * 63;
          ty = hit.y + 22;
        } else if (hit.row === 3) {
          tx = 34 + clampCursorSpeed(state.settings.cursorSpeed) * 63;
          ty = hit.y + 22;
        } else {
          tx = 250;
        }
      }
    }
  } else if (state.sui.screen === 'SETTINGS_DISPLAY') {
    const rel = Math.max(0, Math.min(2, state.sui.displayCursor - state.sui.displayScroll));
    tx = 160;
    ty = 30 + rel * 31 + 14;
  } else if (state.sui.screen === 'SETTINGS_ANIMATIONS') {
    const ac = Math.max(0, Math.min(NL_ANIM_CAT_COUNT - 1, state.sui.animCursor));
    tx = 160;
    ty = 48 + ac * 28 + 12;
  } else if (state.sui.screen === 'SETTINGS_PROFILES') {
    if (state.sui.profMode === 'LIST') {
      const rel = Math.max(0, Math.min(5, state.sui.profCursor - state.sui.profScroll));
      tx = 160;
      ty = 32 + rel * 30 + 14;
    } else if (state.sui.profMode === 'EDIT_AVATAR' || state.sui.profMode === 'CREATE_AVATAR') {
      const cols = state.sui.profAvatarCat === 0 ? 8 : 5;
      const col = state.sui.profAvatarCursor % cols;
      const row = Math.min(2, Math.trunc(state.sui.profAvatarCursor / cols));
      tx = 44 + col * 38;
      ty = 82 + row * 54;
    } else {
      tx = 160;
      ty = 120;
    }
  } else if (state.sui.screen === 'SETTINGS_DIAG') {
    if (state.sui.diagTab === 0) {
      if (state.sui.diagCursor >= state.diag.entries.length) {
        tx = 160;
        ty = 204;
      } else {
        const rel = Math.max(0, Math.min(3, state.sui.diagCursor - state.sui.diagScroll));
        tx = 160;
        ty = 81 + rel * 30;
      }
    } else {
      tx = 160;
      ty = 120;
    }
  }

  return {
    x: Math.max(6, Math.min(NL_SCREEN_W - 6, tx)),
    y: Math.max(4, Math.min(NL_SCREEN_H - 6, ty)),
  };
}

export function setTouchpadControl(
  state: LauncherState,
  tpCursor: 0 | 1 | 2 | 3 | 4 | 5 | 6,
  sensitivity?: number,
  toggleNav?: boolean,
  cursorSpeed?: number,
  cursorStyle?: CursorStyle,
  tpTab?: 0 | 1,
  toggleMode?: boolean,
  explicitMode?: TouchpadMode,
  toggleTapToClick?: boolean,
  cursorSize?: CursorSize
): LauncherState {
  if (state.sui.screen !== 'SETTINGS_TOUCHPAD') return state;
  const nextScroll = Math.min(
    NL_TP_TAB0_ROW_COUNT - NL_TP_TAB0_VISIBLE_ROWS,
    adjustScroll(tpCursor, state.sui.tpScroll, NL_TP_TAB0_VISIBLE_ROWS)
  );
  let nextState: LauncherState = {
    ...state,
    sui: {
      ...state.sui,
      tpCursor,
      tpScroll: nextScroll,
      tpTab: tpTab ?? state.sui.tpTab,
    },
  };
  if (toggleMode || (explicitMode && explicitMode !== nextState.settings.touchpadMode)) {
    const nextMode: TouchpadMode =
      explicitMode ?? (nextState.settings.touchpadMode === 'mouse' ? 'touchpad' : 'mouse');
    const isMouse = nextMode === 'mouse';
    const nextStyle =
      isMouse && nextState.settings.touchpadCursorStyle === 'none'
        ? 'arrow'
        : nextState.settings.touchpadCursorStyle;
    nextState = {
      ...nextState,
      settings: {
        ...nextState.settings,
        touchpadMode: nextMode,
        touchpadCursorShow: isMouse,
        touchpadCursorStyle: nextStyle,
      },
      toastMessage: isMouse ? 'Mode Souris : ACTIF' : 'Mode Pave tactile : ACTIF',
      toastSeq: nextState.toastSeq + 1,
    };
  }
  if (toggleNav) {
    const nextTp = !nextState.settings.touchpadNav;
    nextState = {
      ...nextState,
      settings: { ...nextState.settings, touchpadNav: nextTp },
      toastMessage: nextTp ? 'Glissement au pave : ON' : 'Glissement au pave : OFF',
      toastSeq: nextState.toastSeq + 1,
    };
  }
  if (typeof sensitivity === 'number') {
    const clamped = clampTouchpadSensitivity(sensitivity);
    if (clamped !== nextState.settings.touchpadSensitivity) {
      nextState = {
        ...nextState,
        settings: { ...nextState.settings, touchpadSensitivity: clamped },
        toastMessage: `Sensibilite pave : ${clamped + 1}/5`,
        toastSeq: nextState.toastSeq + 1,
      };
    }
  }
  if (typeof cursorSpeed === 'number') {
    const clampedSpd = clampCursorSpeed(cursorSpeed);
    if (clampedSpd !== nextState.settings.cursorSpeed) {
      nextState = {
        ...nextState,
        settings: { ...nextState.settings, cursorSpeed: clampedSpd },
        toastMessage: `Vitesse souris : ${clampedSpd + 1}/5`,
        toastSeq: nextState.toastSeq + 1,
      };
    }
  }
  if (toggleTapToClick) {
    const nextTap = !nextState.settings.tapToClick;
    nextState = {
      ...nextState,
      settings: { ...nextState.settings, tapToClick: nextTap },
      toastMessage: nextTap ? 'Tapotement pour cliquer : OUI' : 'Tapotement pour cliquer : NON',
      toastSeq: nextState.toastSeq + 1,
    };
  }
  if (cursorStyle && cursorStyle !== nextState.settings.touchpadCursorStyle) {
    const resolvedStyle: CursorStyle = cursorStyle === 'none' ? 'arrow' : cursorStyle;
    const styleIdx = clampCursorStyleToCard(resolvedStyle);
    nextState = {
      ...nextState,
      settings: { ...nextState.settings, touchpadCursorStyle: resolvedStyle },
      sui: { ...nextState.sui, tpCustomRow: 0, tpStyleCursor: styleIdx },
      toastMessage: `Curseur : ${CURSOR_STYLE_LABELS[resolvedStyle]}`,
      toastSeq: nextState.toastSeq + 1,
    };
  }
  if (cursorSize && cursorSize !== nextState.settings.cursorSize) {
    const resolvedSize = clampCursorSize(cursorSize);
    const sizeIdx = cursorSizeToIndex(resolvedSize);
    nextState = {
      ...nextState,
      settings: { ...nextState.settings, cursorSize: resolvedSize },
      sui: { ...nextState.sui, tpCustomRow: 1, tpSizeCursor: sizeIdx },
      toastMessage: `Taille curseur : ${CURSOR_SIZE_LABELS[resolvedSize]}`,
      toastSeq: nextState.toastSeq + 1,
    };
  }
  return updateMouseHover(nextState);
}

function getSettingsScreenTitleLength(state: LauncherState): number {
  const s = state.sui;
  switch (s.screen) {
    case 'SETTINGS_MAIN':
      return 'PARAMETRES'.length;
    case 'SETTINGS_DISPLAY':
      return 'AFFICHAGE'.length;
    case 'SETTINGS_ANIMATIONS':
      return 'ANIMATIONS'.length;
    case 'SETTINGS_TOUCHPAD':
      return (s.tpTab === 1 ? 'PERSONNALISER LE CURSEUR' : 'NAVIGATION TACTILE').length;
    case 'SETTINGS_SHORTCUTS':
      return 'TOUCHES DE LANCEMENT'.length;
    case 'SETTINGS_KEY_HELP':
      return 'COMMANDES DU LAUNCHER'.length;
    case 'SETTINGS_SORT':
      return 'TRI DE LA BIBLIOTHEQUE'.length;
    case 'SETTINGS_STATS':
      return 'STATISTIQUES DE JEU'.length;
    case 'SETTINGS_CAPTURE_BATCH':
      return "GENERER CAPTURES D'ECRAN".length;
    case 'SETTINGS_ICONS':
      return 'GESTION DES ICONES'.length;
    case 'SETTINGS_RESCAN':
      return 'RESCANNER LES JEUX'.length;
    case 'SETTINGS_HOTKEY':
      return 'RACCOURCI GLOBAL TI-OS'.length;
    case 'SETTINGS_RESET':
      return 'REINITIALISER LES REGLAGES'.length;
    case 'SETTINGS_INFO':
      return 'INFORMATIONS SYSTEME'.length;
    case 'SETTINGS_CREDITS':
      return 'A PROPOS ET CREDITS'.length;
    case 'SETTINGS_DIAG':
      return 'DIAGNOSTICS & JOURNAUX'.length;
    case 'SETTINGS_PROFILES':
      if (s.profMode === 'DETAIL' || s.profMode === 'CONFIRM_DELETE') return 'GESTION DU PROFIL'.length;
      if (s.profMode === 'EDIT_NAME') return 'MODIFIER LE NOM'.length;
      if (s.profMode === 'CREATE_NAME') return 'NOUVEAU PROFIL (1/2)'.length;
      if (s.profMode === 'CREATE_AVATAR') return 'NOUVEAU PROFIL (2/2)'.length;
      if (s.profMode === 'EDIT_AVATAR') return 'CHOISIR UNE ICONE'.length;
      return 'PROFILS UTILISATEURS'.length;
    default:
      return 12;
  }
}

export function computePaginationRowMetrics(totalPages: number): {
  startX: number;
  endX: number;
  dotW: number;
  gap: number;
  activeExtra: number;
} {
  const tp = Math.max(1, totalPages);
  if (tp <= NL_PAGINATION_MAX_DOTS) {
    const dotW = 8;
    const capW = 18;
    const gap = 6;
    const totalW = (tp - 1) * (dotW + gap) + capW;
    const startX = Math.floor((NL_SCREEN_W - totalW) / 2);
    return { startX, endX: startX + totalW, dotW, gap, activeExtra: capW - dotW };
  }
  const maxRowW = 280;
  const activeExtra = tp <= 36 ? 6 : tp <= 64 ? 4 : 2;
  let gap = tp <= 32 ? 4 : tp <= 56 ? 3 : tp <= 84 ? 2 : 1;
  const avail = maxRowW - (tp - 1) * gap - activeExtra;
  let dotW = Math.floor(avail / tp);
  if (dotW < 1) dotW = 1;
  if (dotW > 10) dotW = 10;
  let totalW = tp * dotW + (tp - 1) * gap + activeExtra;
  if (totalW > maxRowW && gap > 1) {
    gap--;
    totalW = tp * dotW + (tp - 1) * gap + activeExtra;
  }
  const startX = Math.floor((NL_SCREEN_W - totalW) / 2);
  return { startX, endX: startX + totalW, dotW, gap, activeExtra };
}

export function setCtrlMouseActive(state: LauncherState, active: boolean): LauncherState {
  const nextActive = Boolean(active);
  if (Boolean(state.sui.ctrlMouseActive) === nextActive) {
    return state;
  }
  const next: LauncherState = {
    ...state,
    sui: {
      ...state.sui,
      ctrlMouseActive: nextActive,
    },
  };
  return updateMouseHover(next);
}

export function updateMouseHover(state: LauncherState): LauncherState {
  const sui = state.sui;
  let hoverKind: HoverTargetKind = 'NONE';
  let hoverIndex = 0;
  let hoverSub = 0;

  const setHover = (kind: HoverTargetKind, index = 0, sub = 0): LauncherState => ({
    ...state,
    sui: {
      ...sui,
      hoverValid: kind !== 'NONE',
      hoverKind: kind,
      hoverIndex: index,
      hoverSub: sub,
    },
  });

  if (!isMouseModeActive(state) || state.runningGameIndex !== null) {
    if (!sui.hoverValid && sui.hoverKind === 'NONE') return state;
    return setHover('NONE', 0, 0);
  }

  const mx = Math.max(0, Math.min(NL_SCREEN_W - 1, Math.round(sui.mouseX)));
  const my = Math.max(0, Math.min(NL_SCREEN_H - 1, Math.round(sui.mouseY)));

  if (sui.screen === 'LIBRARY') {
    if (sui.qpOpen) {
      const pcount = Math.max(1, state.profiles.items.length);
      const totalSlots = pcount + 1;
      const visMax = 6;
      const diam = 34;
      const stride = 44;
      const cur = Math.max(0, Math.min(totalSlots - 1, sui.qpCursor));
      const shown = Math.min(totalSlots, visMax);
      let winStart = 0;
      if (totalSlots > visMax) {
        winStart = Math.max(0, Math.min(totalSlots - visMax, cur - Math.trunc(visMax / 2)));
      }
      const stripW = shown * diam + (shown - 1) * (stride - diam);
      const startX = 160 - Math.trunc(stripW / 2);

      if (winStart > 0 && mx >= 12 && mx <= 26 && my >= 102 && my <= 126) {
        return setHover('QP_PREV');
      }
      if (winStart + shown < totalSlots && mx >= 294 && mx <= 308 && my >= 102 && my <= 126) {
        return setHover('QP_NEXT');
      }
      if (mx < 10 || mx >= 310 || my < 52 || my >= 188) {
        return setHover('QP_OUTSIDE');
      }
      for (let v = 0; v < shown; v++) {
        const scx = startX + v * stride + Math.trunc(diam / 2);
        if (mx >= scx - 19 && mx <= scx + 19 && my >= 90 && my <= 140) {
          return setHover('QP_SLOT', winStart + v);
        }
      }
      return setHover('NONE');
    }

    if (my >= 2 && my <= 24) {
      if (mx >= 8 && mx <= 27) return setHover('PROFILE_BADGE');
      if (mx >= 34 && mx < 70) return setHover('CATEGORY', 0);
      if (mx >= 76 && mx < 106) return setHover('CATEGORY', 1);
      if (mx >= 112 && mx < 142) return setHover('CATEGORY', 2);
      if (mx >= 148 && mx <= 164) return setHover('FAV_BTN');
      if (mx >= 293 && mx <= 313) return setHover('SETTINGS_BTN');
      return setHover('NONE');
    }

    if (sui.searchActive && my >= 27 && my <= 51) {
      if (mx >= 278 && mx <= 310) return setHover('SEARCH_CLOSE');
      if (mx >= 10 && mx < 278) return setHover('SEARCH_BAR');
      return setHover('NONE');
    }

    const lm = getLayoutMetrics(
      state.settings.layout,
      state.settings.showNames,
      state.settings.showShortcuts,
      state.filteredIndices.length,
      state.settings.scrollDir ?? 'vertical'
    );

    if (state.totalPages > 1 && my >= lm.paginationY - 4 && my <= lm.paginationY + 12) {
      const pm = computePaginationRowMetrics(state.totalPages);
      const leftX0 = pm.startX >= 19 ? pm.startX - 18 : 2;
      const rightX1 = pm.endX + 18 <= NL_SCREEN_W - 2 ? pm.endX + 18 : NL_SCREEN_W - 2;
      if (mx >= leftX0 && mx < pm.startX - 1) {
        return setHover('PAGE_PREV');
      }
      if (mx > pm.endX + 1 && mx <= rightX1) {
        return setHover('PAGE_NEXT');
      }
      if (mx >= pm.startX && mx <= pm.endX) {
        let cx = pm.startX;
        for (let pi = 0; pi < state.totalPages; pi++) {
          const pw = pm.dotW + (pi === state.currentPage ? pm.activeExtra : 0);
          if (mx >= cx - 1 && mx <= cx + pw + 1) {
            return setHover('PAGE_DOT', pi);
          }
          cx += pw + pm.gap;
        }
      }
    }

    const searchH = sui.searchActive ? 28 : 0;
    const gridClipTop = NL_VIEW_TOP + searchH;
    const gridClipBot = lm.viewBottom;
    if (state.filteredIndices.length > 0 && my >= gridClipTop && my < gridClipBot) {
      const scrollX = lm.isHorizontal ? state.currentPage * lm.pageStrideX : 0;
      const scrollY = lm.isHorizontal ? 0 : state.currentPage * lm.pageStrideY;
      for (let fIdx = 0; fIdx < state.filteredIndices.length; fIdx++) {
        const cardPage = Math.floor(fIdx / lm.perPage);
        const row = Math.floor(fIdx / lm.cols);
        const slot = computeGridSlotPos(lm, state.filteredIndices.length, fIdx);
        const bx = slot.x - scrollX;
        let by = slot.y + searchH - scrollY;
        if (!lm.isHorizontal && searchH === 0 && by < NL_VIEW_TOP && row === state.currentPage * lm.rows) {
          by = NL_VIEW_TOP + 1;
        }
        if (bx + lm.cardW <= 0 || bx >= NL_SCREEN_W) continue;
        if (by + lm.cardH + lm.labelH <= gridClipTop || by >= gridClipBot) continue;
        if (mx >= bx && mx < bx + lm.cardW && my >= by && my < by + lm.cardH + lm.labelH) {
          /* Hovering/clicking ANY peeking icon from a neighboring page (left/right in horizontal OR
           * top/bottom in vertical) navigates directly to that page without launching a game! */
          if (cardPage !== state.currentPage) {
            return setHover(cardPage < state.currentPage ? 'PAGE_PREV' : 'PAGE_NEXT');
          }
          return setHover('GAME_CARD', fIdx);
        }
      }
    }
    return setHover('NONE');
  }

  /* Settings Top Chrome */
  if (my >= 2 && my <= 24) {
    if (mx >= 248 && mx <= 312) {
      return setHover('SET_ESC');
    }
    if (
      sui.screen !== 'SETTINGS_SHORTCUTS' &&
      sui.screen !== 'SETTINGS_ICONS' &&
      sui.screen !== 'SETTINGS_PROFILES' &&
      sui.screen !== 'SETTINGS_DIAG'
    ) {
      const tLen = Math.min(26, getSettingsScreenTitleLength(state));
      const pwrX = 32 + tLen * 6 + 6;
      if (pwrX + 17 <= 246) {
        const pwrEnd = pwrX + 42 <= 246 ? pwrX + 42 : pwrX + 18;
        if (mx >= pwrX && mx <= pwrEnd) {
          return setHover('SET_POWER');
        }
      }
    }
    return setHover('NONE');
  }

  /* Modals Take Priority */
  if (sui.screen === 'SETTINGS_RESET') {
    if (my >= 166 && my <= 192) {
      if (mx >= 42 && mx <= 150) return setHover('MODAL_BTN', 0);
      if (mx >= 170 && mx <= 278) return setHover('MODAL_BTN', 1);
    }
    return setHover('NONE');
  }
  if (sui.screen === 'SETTINGS_ICONS' && sui.iconConfirmModal) {
    if (my >= 138 && my <= 168) {
      if (mx >= 46 && mx <= 148) return setHover('MODAL_BTN', 0);
      if (mx >= 172 && mx <= 274) return setHover('MODAL_BTN', 1);
    }
    return setHover('NONE');
  }
  if (sui.screen === 'SETTINGS_SHORTCUTS' && sui.scConflictPrompt) {
    if (my >= 138 && my <= 162) {
      if (mx >= 54 && mx <= 146) return setHover('MODAL_BTN', 0);
      if (mx >= 174 && mx <= 266) return setHover('MODAL_BTN', 1);
    }
    return setHover('NONE');
  }
  if (sui.screen === 'SETTINGS_SHORTCUTS' && sui.scPickingKey) {
    const totalOpts = ASSIGNABLE_SHORTCUT_KEYS.length + 1;
    for (let k = 0; k < totalOpts; k++) {
      const r = Math.floor(k / 8);
      const c = k % 8;
      const kx = 34 + c * 31;
      const ky = 72 + r * 26;
      const kw = k === 0 ? 28 : 26;
      if (mx >= kx && mx < kx + kw && my >= ky && my < ky + 20) {
        return setHover('SC_KEY_CELL', k);
      }
    }
    return setHover('NONE');
  }
  if (sui.screen === 'SETTINGS_PROFILES' && sui.profMode === 'CONFIRM_DELETE') {
    if (my >= 148 && my <= 172) {
      if (mx >= 42 && mx <= 146) return setHover('MODAL_BTN', 0);
      if (mx >= 174 && mx <= 278) return setHover('MODAL_BTN', 1);
    }
    return setHover('NONE');
  }
  if (sui.screen === 'SETTINGS_DIAG' && sui.diagConfirmModal) {
    if (my >= 152 && my <= 176) {
      if (mx >= 42 && mx <= 150) return setHover('MODAL_BTN', 0);
      if (mx >= 170 && mx <= 278) return setHover('MODAL_BTN', 1);
    }
    return setHover('NONE');
  }
  if (sui.screen === 'SETTINGS_DIAG' && sui.diagDetailOpen) {
    if (mx >= 12 && mx < 308 && my >= 30 && my <= 214) {
      return setHover('DIAG_DETAIL_CLOSE', 0);
    }
    return setHover('NONE');
  }

  switch (sui.screen) {
    case 'SETTINGS_MAIN': {
      if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
        const spx = sui.menuScroll * 30;
        for (let idx = 0; idx < SETTINGS_MENU_ITEMS.length; idx++) {
          const ry = 32 + idx * 30 - spx;
          if (ry + 28 <= 28 || ry >= 218) continue;
          if (my >= ry && my < ry + 28) {
            return setHover('SET_ROW', idx);
          }
        }
      }
      break;
    }
    case 'SETTINGS_RESCAN': {
      if (sui.rescan.emptyConfirmPending && my >= 166 && my <= 194) {
        if (mx >= 42 && mx <= 150) return setHover('MODAL_BTN', 0);
        if (mx >= 170 && mx <= 278) return setHover('MODAL_BTN', 1);
      }
      break;
    }
    case 'SETTINGS_HOTKEY': {
      if (mx >= 14 && mx < 306 && my >= 32 && my < 76) {
        return setHover('SET_ROW', 0);
      }
      if (mx >= 14 && mx < 306 && my >= 80 && my < 112) {
        return setHover('SET_ROW', 1);
      }
      break;
    }
    case 'SETTINGS_DISPLAY': {
      if (mx >= 12 && mx < 304 && my >= 28 && my < 122) {
        const spx = sui.displayScroll * 31;
        for (let r = 0; r < NL_DISPLAY_OPT_COUNT; r++) {
          const ry = 30 + r * 31 - spx;
          if (ry + 28 <= 28 || ry >= 122) continue;
          if (my >= ry && my < ry + 28) {
            return setHover('SET_ROW', r);
          }
        }
      }
      break;
    }
    case 'SETTINGS_ANIMATIONS': {
      if (mx >= 14 && mx < 306 && my >= 48 && my < 164) {
        for (let r = 0; r < NL_ANIM_CAT_COUNT; r++) {
          const ry = 48 + r * 28;
          if (my >= ry && my < ry + 25) {
            return setHover('SET_ROW', r);
          }
        }
      }
      break;
    }
    case 'SETTINGS_TOUCHPAD': {
      if (my >= 28 && my <= 44) {
        if (mx >= 15 && mx <= 153) return setHover('SET_TAB', 0);
        if (mx >= 155 && mx <= 305) return setHover('SET_TAB', 1);
      }
      if (sui.tpTab === 1) {
        if (my >= 50 && my <= 134) {
          for (let c = 0; c < 3; c++) {
            const cx = 14 + c * 101;
            if (mx >= cx && mx < cx + 90) {
              return setHover('SET_STYLE_CARD', c);
            }
          }
        } else if (my >= 142 && my <= 214) {
          for (let s = 0; s < 3; s++) {
            const cx = 14 + s * 101;
            if (mx >= cx && mx < cx + 90) {
              return setHover('SET_SIZE_CARD', s);
            }
          }
        }
      } else if (mx >= 14 && mx < 306) {
        const rects = getTouchpadTab0RowRects(sui.tpScroll);
        for (const r of rects) {
          if (my >= r.y && my < r.y + r.h) {
            if ((r.row === 2 || r.row === 3) && my >= r.y + 15 && mx >= 22 && mx <= 298) {
              const cnt = r.row === 2 ? NL_TP_SENS_COUNT : NL_CURSOR_SPEED_COUNT;
              let bestN = 0;
              let bestD = 9999;
              for (let n = 0; n < cnt; n++) {
                const d = Math.abs(mx - (34 + n * 63));
                if (d < bestD) {
                  bestD = d;
                  bestN = n;
                }
              }
              return setHover('SET_SLIDER_NOTCH', r.row, bestN);
            }
            return setHover('SET_ROW', r.row);
          }
        }
      }
      break;
    }
    case 'SETTINGS_SHORTCUTS': {
      if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
        const spx = sui.scGameScroll * 30;
        for (let idx = 0; idx < state.games.length; idx++) {
          const ry = 32 + idx * 30 - spx;
          if (ry + 28 <= 28 || ry >= 218) continue;
          if (my >= ry && my < ry + 28) return setHover('SET_ROW', idx);
        }
      }
      break;
    }
    case 'SETTINGS_SORT': {
      if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
        const spx = sui.subScroll * 30;
        for (let idx = 0; idx < SORT_ORDER_LIST.length; idx++) {
          const ry = 32 + idx * 30 - spx;
          if (ry + 28 <= 28 || ry >= 218) continue;
          if (my >= ry && my < ry + 28) return setHover('SET_ROW', idx);
        }
      }
      break;
    }
    case 'SETTINGS_STATS': {
      if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
        const spx = sui.subScroll * 36;
        for (let idx = 0; idx < state.games.length; idx++) {
          const ry = 33 + idx * 36 - spx;
          if (ry + 33 <= 28 || ry >= 218) continue;
          if (my >= ry && my < ry + 33) return setHover('SET_ROW', idx);
        }
      }
      break;
    }
    case 'SETTINGS_CAPTURE_BATCH': {
      if (sui.capActive && sui.capTotal > 0) {
        if (my >= 174 && my <= 198) {
          if (mx >= 30 && mx <= 154) return setHover('SET_ROW', 0);
          if (mx >= 166 && mx <= 290) return setHover('SET_ROW', 1);
        }
      } else if (mx >= 96 && mx <= 224 && my >= 168 && my <= 198) {
        return setHover('SET_ROW', 0);
      }
      break;
    }
    case 'SETTINGS_ICONS': {
      if (state.games.length > 0 && my >= 28 && my < 216) {
        for (let idx = 0; idx < state.games.length; idx++) {
          const r = Math.floor(idx / 3);
          const c = idx % 3;
          const cx = 32 + c * 90;
          const cy = 33 + (r - sui.iconScrollRow) * 90;
          if (cy + 86 <= 28 || cy >= 216) continue;
          if (mx >= cx && mx < cx + 68 && my >= cy && my < cy + 84) {
            return setHover('ICON_CARD', idx);
          }
        }
      }
      break;
    }
    case 'SETTINGS_PROFILES': {
      if (sui.profMode === 'LIST') {
        const totalRows = state.profiles.items.length + 2;
        if (mx >= 12 && mx < 304 && my >= 28 && my < 218) {
          const spx = sui.profScroll * 30;
          for (let row = 0; row < totalRows; row++) {
            const ry = 32 + row * 30 - spx;
            if (ry + 28 <= 28 || ry >= 218) continue;
            if (my >= ry && my < ry + 28) return setHover('PROF_ROW', row);
          }
        }
      } else if (sui.profMode === 'DETAIL') {
        const detailRows = state.settings.profileLockEnabled ? 5 : 4;
        const rowY0 = detailRows === 5 ? 80 : 83;
        const rowH = detailRows === 5 ? 24 : 28;
        const stride = detailRows === 5 ? 27 : 32;
        if (mx >= 13 && mx < 307) {
          for (let r = 0; r < detailRows; r++) {
            const ry = rowY0 + r * stride;
            if (my >= ry && my < ry + rowH) return setHover('PROF_DETAIL_ROW', r);
          }
        }
      } else if (sui.profMode === 'EDIT_AVATAR' || sui.profMode === 'CREATE_AVATAR') {
        if (my >= 30 && my <= 46) {
          for (let c = 0; c < NL_AVATAR_CAT_COUNT; c++) {
            const tx = 11 + c * 75;
            if (mx >= tx && mx < tx + 72) return setHover('PROF_AV_TAB', c);
          }
        }
        const cat = Math.max(0, Math.min(NL_AVATAR_CAT_COUNT - 1, sui.profAvatarCat));
        const { count: ccnt } = getAvatarCategoryRange(cat);
        const cols = cat === 0 ? 8 : 5;
        const diam = cat === 0 ? 31 : 36;
        const dxStep = cat === 0 ? 37 : 52;
        const dyStep = cat === 0 ? 54 : 58;
        const gridW = (cols - 1) * dxStep + diam;
        const startX = Math.floor((NL_SCREEN_W - gridW) / 2);
        const startY = 58;
        if (my >= 48 && my < 198) {
          for (let i = 0; i < ccnt; i++) {
            const r = Math.floor(i / cols);
            const colIdx = i % cols;
            const ax = startX + colIdx * dxStep;
            const ay = startY + r * dyStep;
            if (mx >= ax - 2 && mx < ax + diam + 2 && my >= ay - 2 && my < ay + diam + 2) {
              return setHover('PROF_AV_CELL', i);
            }
          }
        }
      }
      break;
    }
    case 'SETTINGS_DIAG': {
      if (my >= 28 && my <= 44) {
        if (mx >= 14 && mx <= 156) return setHover('DIAG_TAB', 0);
        if (mx >= 160 && mx <= 306) return setHover('DIAG_TAB', 1);
      }
      if (sui.diagTab === 0) {
        if (my >= 193 && my <= 215 && mx >= 14 && mx <= 306) {
          return setHover('DIAG_CLEAR_BTN', 0);
        }
        if (state.diag.entries.length > 0 && mx >= 14 && mx <= 302 && my >= 66 && my < 190) {
          for (let i = 0; i < state.diag.entries.length; i++) {
            const ry = 67 + (i - sui.diagScroll) * 30;
            if (ry + 28 <= 66 || ry >= 190) continue;
            if (my >= ry && my < ry + 28) {
              return setHover('DIAG_ROW', i);
            }
          }
        }
      }
      break;
    }
    default:
      break;
  }

  return setHover(hoverKind, hoverIndex, hoverSub);
}

export function moveMouseTo(state: LauncherState, xPx: number, yPx: number): LauncherState {
  const cx = Math.max(0, Math.min(NL_SCREEN_W - 1, Math.round(xPx)));
  const cy = Math.max(0, Math.min(NL_SCREEN_H - 1, Math.round(yPx)));
  const next: LauncherState = {
    ...state,
    mouseSeq: (state.mouseSeq ?? 0) + 1,
    sui: {
      ...state.sui,
      mouseX: cx,
      mouseY: cy,
    },
  };
  return updateMouseHover(next);
}

export function feedMouseTouchpadDelta(
  state: LauncherState,
  tracker: TouchpadTracker,
  contact: boolean,
  rawX: number,
  rawY: number,
  padWidth = 2328,
  padHeight = 1691,
  invertY = true
): { state: LauncherState; moved: boolean } {
  if (!isMouseModeActive(state)) {
    return { state, moved: false };
  }
  if (!contact) {
    resetTouchpadTracker(tracker);
    return { state, moved: false };
  }

  const w = padWidth >= 200 ? padWidth : 2328;
  const h = padHeight >= 200 ? padHeight : 1691;

  if (!tracker.touching) {
    tracker.touching = true;
    tracker.lastX = rawX;
    tracker.lastY = rawY;
    tracker.accX = 0;
    tracker.accY = 0;
    tracker.activeFrames = 1;
    return { state: updateMouseHover(state), moved: false };
  }

  tracker.activeFrames++;
  const dxRaw = rawX - tracker.lastX;
  const dyScreen = invertY ? tracker.lastY - rawY : rawY - tracker.lastY;
  tracker.lastX = rawX;
  tracker.lastY = rawY;

  const dxNorm = Math.trunc((dxRaw * 1024) / w);
  const dyNorm = Math.trunc((dyScreen * 1024) / h);

  if (Math.abs(dxNorm) > 460 || Math.abs(dyNorm) > 460) {
    return { state, moved: false };
  }
  if (Math.abs(dxNorm) < 3 && Math.abs(dyNorm) < 3) {
    return { state, moved: false };
  }

  const scale = NL_CURSOR_SPEED_SCALES[clampCursorSpeed(state.settings.cursorSpeed)];
  let dxPx = (dxNorm * scale) / 64;
  let dyPx = (dyNorm * scale * 12) / (64 * 16);
  if (dxPx === 0 && Math.abs(dxNorm) >= 3) dxPx = dxNorm > 0 ? 0.25 : -0.25;
  if (dyPx === 0 && Math.abs(dyNorm) >= 3) dyPx = dyNorm > 0 ? 0.25 : -0.25;

  const prevX = Math.round(state.sui.mouseX);
  const prevY = Math.round(state.sui.mouseY);
  const nextX = Math.max(0, Math.min(NL_SCREEN_W - 1, state.sui.mouseX + dxPx));
  const nextY = Math.max(0, Math.min(NL_SCREEN_H - 1, state.sui.mouseY + dyPx));
  const moved = Math.round(nextX) !== prevX || Math.round(nextY) !== prevY || Math.abs(dxPx) > 0 || Math.abs(dyPx) > 0;

  const updated = updateMouseHover({
    ...state,
    mouseSeq: moved ? (state.mouseSeq ?? 0) + 1 : state.mouseSeq,
    sui: {
      ...state.sui,
      mouseX: nextX,
      mouseY: nextY,
    },
  });
  return { state: updated, moved };
}

export function clickMouse(
  state: LauncherState,
  nowSec = Math.floor(Date.now() / 1000)
): { state: LauncherState; handled: boolean } {
  if (state.runningGameIndex !== null) {
    return { state, handled: false };
  }
  const hovered = updateMouseHover(state);
  const sui = hovered.sui;
  if (!sui.hoverValid || sui.hoverKind === 'NONE') {
    return { state: hovered, handled: false };
  }

  const hk = sui.hoverKind;
  const hi = sui.hoverIndex;
  const hs = sui.hoverSub;

  let next = hovered;
  let handled = true;

  switch (hk) {
    case 'PROFILE_BADGE':
      next = handleLauncherKey(hovered, 'PROFILE_QUICK', nowSec);
      break;

    case 'CATEGORY': {
      const cat = CATEGORY_ORDER[hi];
      if (cat && cat !== hovered.activeCategory) {
        next = snapshotActiveProfile(
          rebuildFilter({
            ...hovered,
            activeCategory: cat,
            selectedPos: 0,
            navSeq: hovered.navSeq + 1,
          })
        );
      } else {
        handled = false;
      }
      break;
    }

    case 'FAV_BTN':
      if (hovered.filteredIndices.length > 0) {
        next = handleLauncherKey(hovered, 'FAV', nowSec);
      } else {
        handled = false;
      }
      break;

    case 'SETTINGS_BTN':
      next = handleLauncherKey(hovered, 'SETTINGS', nowSec);
      break;

    case 'SEARCH_CLOSE':
      if (sui.searchActive) {
        next = handleLauncherKey(hovered, 'ESC', nowSec);
      } else {
        handled = false;
      }
      break;

    case 'SEARCH_BAR':
      handled = true;
      break;

    case 'PAGE_PREV':
      next = handleLauncherKey(hovered, 'PAGE_PREV', nowSec);
      break;

    case 'PAGE_NEXT':
      next = handleLauncherKey(hovered, 'PAGE_NEXT', nowSec);
      break;

    case 'PAGE_DOT':
      if (hovered.filteredIndices.length > 0 && hovered.totalPages > 1 && hi >= 0 && hi < hovered.totalPages) {
        const m = getLayoutMetrics(
          hovered.settings.layout,
          hovered.settings.showNames,
          hovered.settings.showShortcuts,
          hovered.filteredIndices.length,
          hovered.settings.scrollDir ?? 'vertical'
        );
        const newPos = Math.min(hovered.filteredIndices.length - 1, hi * m.perPage);
        next = {
          ...hovered,
          selectedPos: newPos,
          currentPage: hi,
          navSeq: hovered.navSeq + 1,
        };
      } else {
        handled = false;
      }
      break;

    case 'GAME_CARD':
      if (hi >= 0 && hi < hovered.filteredIndices.length) {
        const mCard = getLayoutMetrics(
          hovered.settings.layout,
          hovered.settings.showNames,
          hovered.settings.showShortcuts,
          hovered.filteredIndices.length,
          hovered.settings.scrollDir ?? 'vertical'
        );
        const cardPage = Math.floor(hi / mCard.perPage);
        if (cardPage !== hovered.currentPage) {
          next = handleLauncherKey(hovered, cardPage < hovered.currentPage ? 'PAGE_PREV' : 'PAGE_NEXT', nowSec);
        } else {
          const gIdx = hovered.filteredIndices[hi];
          next = triggerGameLaunch(
            {
              ...hovered,
              selectedPos: hi,
            },
            gIdx,
            nowSec
          );
        }
      } else {
        handled = false;
      }
      break;

    case 'QP_PREV':
      next = sui.qpOpen ? handleLauncherKey(hovered, 'LEFT', nowSec) : hovered;
      break;

    case 'QP_NEXT':
      next = sui.qpOpen ? handleLauncherKey(hovered, 'RIGHT', nowSec) : hovered;
      break;

    case 'QP_OUTSIDE':
      next = sui.qpOpen ? { ...hovered, sui: { ...sui, qpOpen: false } } : hovered;
      break;

    case 'QP_SLOT': {
      const pcount = Math.max(1, hovered.profiles.items.length);
      const totalSlots = pcount + 1;
      if (sui.qpOpen && hi >= 0 && hi < totalSlots) {
        next = handleLauncherKey({ ...hovered, sui: { ...sui, qpCursor: hi } }, 'EXE', nowSec);
      } else {
        handled = false;
      }
      break;
    }

    case 'SET_ESC':
      next = handleLauncherKey(hovered, 'ESC', nowSec);
      break;

    case 'SET_POWER':
      if (
        sui.screen !== 'LIBRARY' &&
        sui.screen !== 'SETTINGS_SHORTCUTS' &&
        sui.screen !== 'SETTINGS_ICONS' &&
        sui.screen !== 'SETTINGS_PROFILES' &&
        sui.screen !== 'SETTINGS_DIAG'
      ) {
        next = requestCalculatorPoweroff(hovered);
      } else {
        handled = false;
      }
      break;

    case 'SET_TAB':
      if (sui.screen === 'SETTINGS_TOUCHPAD' && (hi === 0 || hi === 1) && sui.tpTab !== hi) {
        next = {
          ...hovered,
          sui: {
            ...sui,
            tpTab: hi,
            tpCustomRow: 0,
            tpStyleCursor: clampCursorStyleToCard(hovered.settings.touchpadCursorStyle),
            tpSizeCursor: cursorSizeToIndex(hovered.settings.cursorSize),
          },
        };
      } else {
        handled = false;
      }
      break;

    case 'SET_SLIDER_NOTCH':
      if (sui.screen === 'SETTINGS_TOUCHPAD' && sui.tpTab === 0) {
        if (hi === 2) {
          next = setTouchpadControl(hovered, 2, hs);
        } else if (hi === 3) {
          next = setTouchpadControl(hovered, 3, undefined, undefined, hs);
        } else {
          handled = false;
        }
      } else {
        handled = false;
      }
      break;

    case 'SET_STYLE_CARD':
      if (sui.screen === 'SETTINGS_TOUCHPAD' && sui.tpTab === 1 && hi >= 0 && hi < 3) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, tpCustomRow: 0, tpStyleCursor: hi } },
          'EXE',
          nowSec
        );
      } else {
        handled = false;
      }
      break;

    case 'SET_SIZE_CARD':
      if (sui.screen === 'SETTINGS_TOUCHPAD' && sui.tpTab === 1 && hi >= 0 && hi < 3) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, tpCustomRow: 1, tpSizeCursor: hi } },
          'EXE',
          nowSec
        );
      } else {
        handled = false;
      }
      break;

    case 'SET_ROW':
      if (sui.screen === 'SETTINGS_MAIN' && hi >= 0 && hi < SETTINGS_MENU_ITEMS.length) {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              menuCursor: hi,
              menuScroll: adjustScroll(hi, sui.menuScroll, 6),
            },
          },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_HOTKEY') {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, hotkeyCursor: hi === 1 ? 1 : 0 } },
          hi === 1 ? 'RIGHT' : 'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_DISPLAY' && hi >= 0 && hi < NL_DISPLAY_OPT_COUNT) {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              displayCursor: hi,
              displayScroll: adjustScroll(hi, sui.displayScroll, 3),
            },
          },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_ANIMATIONS' && hi >= 0 && hi < NL_ANIM_CAT_COUNT) {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              animCursor: hi,
            },
          },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_TOUCHPAD' && sui.tpTab === 0 && hi >= 0 && hi < NL_TP_TAB0_ROW_COUNT) {
        if (hi === 2) {
          const nextSens = (clampTouchpadSensitivity(hovered.settings.touchpadSensitivity) + 1) % NL_TP_SENS_COUNT;
          next = setTouchpadControl(hovered, 2, nextSens);
        } else if (hi === 3) {
          const nextSpd = (clampCursorSpeed(hovered.settings.cursorSpeed) + 1) % NL_CURSOR_SPEED_COUNT;
          next = setTouchpadControl(hovered, 3, undefined, undefined, nextSpd);
        } else {
          next = handleLauncherKey(
            { ...hovered, sui: { ...sui, tpCursor: hi } },
            'EXE',
            nowSec
          );
        }
      } else if (sui.screen === 'SETTINGS_SHORTCUTS' && hi >= 0 && hi < hovered.games.length) {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              scGameCursor: hi,
              scGameScroll: adjustScroll(hi, sui.scGameScroll, 6),
            },
          },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_SORT' && hi >= 0 && hi < SORT_ORDER_LIST.length) {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              subCursor: hi,
              subScroll: adjustScroll(hi, sui.subScroll, 6),
            },
          },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_STATS' && hi >= 0 && hi < hovered.games.length) {
        next = {
          ...hovered,
          sui: {
            ...sui,
            subCursor: hi,
            subScroll: adjustScroll(hi, sui.subScroll, 5),
          },
        };
      } else if (sui.screen === 'SETTINGS_CAPTURE_BATCH') {
        if (sui.capActive && sui.capTotal > 0) {
          next = handleLauncherKey(hovered, hi === 0 ? 'RIGHT' : 'ESC', nowSec);
        } else {
          next = handleLauncherKey(hovered, 'EXE', nowSec);
        }
      } else {
        handled = false;
      }
      break;

    case 'SC_KEY_CELL':
      if (sui.screen === 'SETTINGS_SHORTCUTS' && sui.scPickingKey) {
        next = handleLauncherKey({ ...hovered, sui: { ...sui, scKeyCursor: hi } }, 'EXE', nowSec);
      } else {
        handled = false;
      }
      break;

    case 'ICON_CARD':
      if (sui.screen === 'SETTINGS_ICONS' && hi >= 0 && hi < hovered.games.length) {
        next = handleLauncherKey({ ...hovered, sui: { ...sui, iconCursor: hi } }, 'EXE', nowSec);
      } else {
        handled = false;
      }
      break;

    case 'PROF_ROW':
      if (sui.screen === 'SETTINGS_PROFILES' && sui.profMode === 'LIST') {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              profCursor: hi,
              profScroll: adjustScroll(hi, sui.profScroll, 6),
            },
          },
          'EXE',
          nowSec
        );
      } else {
        handled = false;
      }
      break;

    case 'PROF_DETAIL_ROW':
      if (
        sui.screen === 'SETTINGS_PROFILES' &&
        sui.profMode === 'DETAIL' &&
        hi >= 0 &&
        hi < (hovered.settings.profileLockEnabled ? 5 : 4)
      ) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, profDetailCursor: hi } },
          'EXE',
          nowSec
        );
      } else {
        handled = false;
      }
      break;

    case 'PROF_AV_TAB':
      if (
        sui.screen === 'SETTINGS_PROFILES' &&
        (sui.profMode === 'EDIT_AVATAR' || sui.profMode === 'CREATE_AVATAR') &&
        hi >= 0 &&
        hi < NL_AVATAR_CAT_COUNT
      ) {
        next = {
          ...hovered,
          sui: {
            ...sui,
            profAvatarCat: hi,
            profAvatarCursor: 0,
          },
        };
      } else {
        handled = false;
      }
      break;

    case 'PROF_AV_CELL':
      if (
        sui.screen === 'SETTINGS_PROFILES' &&
        (sui.profMode === 'EDIT_AVATAR' || sui.profMode === 'CREATE_AVATAR')
      ) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, profAvatarCursor: hi } },
          'EXE',
          nowSec
        );
      } else {
        handled = false;
      }
      break;

    case 'MODAL_BTN':
      if (sui.screen === 'SETTINGS_RESET') {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, resetConfirmYes: hi === 1 } },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_ICONS' && sui.iconConfirmModal) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, iconConfirmYes: hi === 1 } },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_SHORTCUTS' && sui.scConflictPrompt) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, scConflictConfirmYes: hi === 1 } },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_PROFILES' && sui.profMode === 'CONFIRM_DELETE') {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, profDeleteYes: hi === 1 } },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_DIAG' && sui.diagConfirmModal) {
        next = handleLauncherKey(
          { ...hovered, sui: { ...sui, diagConfirmYes: hi === 1 } },
          'EXE',
          nowSec
        );
      } else if (sui.screen === 'SETTINGS_RESCAN' && sui.rescan.emptyConfirmPending) {
        next = handleLauncherKey(
          {
            ...hovered,
            sui: {
              ...sui,
              rescan: { ...sui.rescan, emptyConfirmReplace: hi === 1 },
            },
          },
          'EXE',
          nowSec
        );
      } else {
        handled = false;
      }
      break;

    case 'DIAG_TAB':
      if (sui.screen === 'SETTINGS_DIAG' && (hi === 0 || hi === 1)) {
        next = {
          ...hovered,
          sui: {
            ...sui,
            diagTab: hi as 0 | 1,
            diagCursor: 0,
            diagScroll: 0,
            diagDetailOpen: false,
          },
        };
      } else {
        handled = false;
      }
      break;

    case 'DIAG_ROW':
      if (sui.screen === 'SETTINGS_DIAG' && sui.diagTab === 0 && hi >= 0 && hi < hovered.diag.entries.length) {
        next = {
          ...hovered,
          sui: {
            ...sui,
            diagCursor: hi,
            diagScroll: adjustScroll(hi, sui.diagScroll, 4),
            diagDetailOpen: true,
          },
        };
      } else {
        handled = false;
      }
      break;

    case 'DIAG_CLEAR_BTN':
      if (sui.screen === 'SETTINGS_DIAG' && sui.diagTab === 0) {
        next = {
          ...hovered,
          sui: {
            ...sui,
            diagCursor: hovered.diag.entries.length,
            diagConfirmModal: true,
            diagConfirmYes: false,
          },
        };
      } else {
        handled = false;
      }
      break;

    case 'DIAG_DETAIL_CLOSE':
      if (sui.screen === 'SETTINGS_DIAG' && sui.diagDetailOpen) {
        next = {
          ...hovered,
          sui: { ...sui, diagDetailOpen: false },
        };
      } else {
        handled = false;
      }
      break;

    default:
      handled = false;
      break;
  }

  return { state: updateMouseHover(next), handled };
}

