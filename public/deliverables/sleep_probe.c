/*
 * sleep_probe.c — Prototype isolé de vérification matérielle (Piste A : Relais Veille TI-OS)
 * Cible : TI-Nspire CX II-T (Ndless)
 *
 * Ce programme est STRICTEMENT INDEPENDANT de N-Launcher.
 * Il ne modifie aucun registre matériel d'alimentation et ne simule aucun écran noir.
 * Il permet de tester sur calculatrice réelle trois variantes de délégation à TI-OS :
 *   [1] A1 : TCT_Local_Control_Interrupts(0) + get_event() sous lcd_blit (320x240 RGB565)
 *   [2] A2 : lcd_init(SCR_TYPE_INVALID) + TCT_Local_Control_Interrupts(0) + get_event()
 *   [3] A3 : lcd_init(SCR_TYPE_INVALID) + dispatcher natif TI-OS (_show_msgbox)
 *   [ESC]  : Quitter proprement vers TI-OS
 ********************************************************************************************/

#include <os.h>
#include <libndls.h>
#include <keys.h>
#include <syscall-list.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SP_W 320
#define SP_H 240
#define SP_FB_BYTES (SP_W * SP_H * sizeof(uint16_t))

#define RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                (((uint16_t)(g) & 0xFCU) << 3) | \
                (((uint16_t)(b) & 0xF8U) >> 3)))

#define COL_BG      RGB565(15, 20, 30)
#define COL_CARD    RGB565(25, 33, 48)
#define COL_BORDER  RGB565(48, 63, 90)
#define COL_ACCENT  RGB565(56, 165, 255)
#define COL_TEXT    RGB565(240, 244, 250)
#define COL_MUTED   RGB565(155, 168, 190)
#define COL_OK      RGB565(58, 205, 125)
#define COL_WARN    RGB565(250, 190, 60)
#define COL_ERR     RGB565(245, 95, 95)

/* GCC 12+ arm-none-eabi newlib compatibility stub */
void __attribute__((weak)) _fini(void) {}

/* Compact 5x7 ASCII font (codes 32..126) */
static const uint8_t k_font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */
    {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x7F,0x41,0x41,0x00}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x00,0x41,0x41,0x7F,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x0C,0x52,0x52,0x52,0x3E}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x7F,0x10,0x28,0x44,0x00}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
    {0x00,0x08,0x36,0x41,0x00}, /* { */
    {0x00,0x00,0x7F,0x00,0x00}, /* | */
    {0x00,0x41,0x36,0x08,0x00}, /* } */
    {0x10,0x08,0x08,0x10,0x08}  /* ~ */
};

static uint16_t *g_fb = NULL;
static void *g_os_vram = NULL;
static int g_entry_intmask = 0;

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!g_fb || w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = (x + w > SP_W) ? SP_W : (x + w);
    int y1 = (y + h > SP_H) ? SP_H : (y + h);
    for (int py = y0; py < y1; py++) {
        uint16_t *row = g_fb + py * SP_W;
        for (int px = x0; px < x1; px++) {
            row[px] = color;
        }
    }
}

static void draw_box(int x, int y, int w, int h, uint16_t bg, uint16_t border) {
    fill_rect(x, y, w, h, border);
    if (w > 2 && h > 2) {
        fill_rect(x + 1, y + 1, w - 2, h - 2, bg);
    }
}

static void draw_text(int x, int y, const char *s, uint16_t color) {
    if (!g_fb || !s) return;
    int cx = x;
    while (*s) {
        unsigned char ch = (unsigned char)(*s++);
        if (ch < 32U || ch > 126U) ch = '?';
        const uint8_t *glyph = k_font5x7[ch - 32U];
        for (int col = 0; col < 5; col++) {
            int px = cx + col;
            if (px < 0 || px >= SP_W) continue;
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                int py = y + row;
                if (py < 0 || py >= SP_H) continue;
                if (bits & (1U << row)) {
                    g_fb[py * SP_W + px] = color;
                }
            }
        }
        cx += 6;
        if (cx >= SP_W) break;
    }
}

static uint32_t read_rtc_sec(void) {
    return *(const volatile uint32_t *)(uintptr_t)0x90090000U;
}

static void safe_delay_ms(unsigned ms) {
    if (is_classic) {
        msleep(ms);
        return;
    }
    volatile unsigned *t2_load = (volatile unsigned *)0x900D0020;
    volatile unsigned *t2_val  = (volatile unsigned *)0x900D0024;
    volatile unsigned *t2_ctrl = (volatile unsigned *)0x900D0028;

    unsigned orig_ctrl = *t2_ctrl;
    unsigned orig_load = *t2_load;
    *t2_ctrl = 0;
    *t2_load = 0xFFFFFFFFU;
    *t2_ctrl = 0x82U;

    unsigned start = *t2_val;
    unsigned target = ms * 33U; /* ~32.768 kHz */
    while ((unsigned)(start - *t2_val) < target) {
        __asm__ volatile("nop");
    }

    *t2_ctrl = 0;
    *t2_load = orig_load;
    *t2_ctrl = orig_ctrl;
}

static void wait_release_all(void) {
    unsigned guard = 0U;
    while ((any_key_pressed() || on_key_pressed()) && guard < 180U) {
        safe_delay_ms(12);
        guard++;
    }
    safe_delay_ms(40);
}

typedef struct {
    bool     has_run;
    char     mode_name[24];
    uint32_t elapsed_sec;
    uint32_t events_seen;
    uint16_t last_ev_type;
    uint32_t last_ev_key;
    bool     saw_apd_event;
    bool     saw_on_key;
    bool     ended_by_esc;
    int      ret_code;
} probe_result_t;

static probe_result_t g_last_res;

static void render_ui(uint32_t start_rtc, uint32_t cycles) {
    uint32_t now_rtc = read_rtc_sec();
    uint32_t uptime = now_rtc - start_rtc;

    fill_rect(0, 0, SP_W, SP_H, COL_BG);

    /* Top bar */
    fill_rect(0, 0, SP_W, 24, COL_CARD);
    fill_rect(0, 24, SP_W, 1, COL_BORDER);
    draw_text(8, 8, "SLEEP PROBE v1  -  TEST PISTE A", COL_ACCENT);

    char rtc_buf[24];
    snprintf(rtc_buf, sizeof(rtc_buf), "RTC:%lus", (unsigned long)uptime);
    draw_text(250, 8, rtc_buf, COL_TEXT);

    /* Hardware info card */
    draw_box(8, 30, 304, 34, COL_CARD, COL_BORDER);
    char hw_line1[56];
    char hw_line2[56];
    snprintf(hw_line1, sizeof(hw_line1), "HW:%s  Ndless:r%u  IntMask:0x%X",
             is_cx2 ? "CX-II-T" : (has_colors ? "CX" : "Classic"),
             nl_ndless_rev(),
             (unsigned)g_entry_intmask);
    snprintf(hw_line2, sizeof(hw_line2), "OS sig:0x%08lX  Cycles:%lu",
             (unsigned long)(*(const volatile uint32_t *)(uintptr_t)0x10000020U),
             (unsigned long)cycles);
    draw_text(14, 36, hw_line1, COL_TEXT);
    draw_text(14, 49, hw_line2, COL_MUTED);

    /* 3 isolated tests */
    draw_box(8, 70, 304, 72, COL_CARD, COL_BORDER);
    draw_text(14, 76, "CHOISIR UN TEST SUR CALCULATRICE :", COL_ACCENT);
    draw_text(14, 91, "[1] A1: Ints(0) + get_event (sous lcd_blit)", COL_TEXT);
    draw_text(14, 105, "[2] A2: Ecran OS + Ints(0) + get_event", COL_TEXT);
    draw_text(14, 119, "[3] A3: Ecran OS + Dispatcher TI-OS (msgbox)", COL_TEXT);
    draw_text(14, 131, "[ESC] Quitter proprement vers TI-OS", COL_WARN);

    /* Last test result */
    draw_box(8, 148, 304, 84, COL_CARD, COL_BORDER);
    if (!g_last_res.has_run) {
        draw_text(14, 156, "DERNIER RESULTAT : Aucun test lance.", COL_MUTED);
        draw_text(14, 172, "Reglez la veille TI-OS au minimum (ex: 1 min)", COL_MUTED);
        draw_text(14, 185, "puis lancez [1], [2] ou [3] sans toucher", COL_MUTED);
        draw_text(14, 198, "aux touches jusqu'a expiration du delai.", COL_MUTED);
    } else {
        char l1[56], l2[56], l3[56], l4[56];
        snprintf(l1, sizeof(l1), "RESULTAT %s  Duree:%lus  Evts:%lu",
                 g_last_res.mode_name,
                 (unsigned long)g_last_res.elapsed_sec,
                 (unsigned long)g_last_res.events_seen);
        snprintf(l2, sizeof(l2), "Last ev type=0x%02X key=0x%04lX ret=%d",
                 (unsigned)g_last_res.last_ev_type,
                 (unsigned long)g_last_res.last_ev_key,
                 g_last_res.ret_code);
        snprintf(l3, sizeof(l3), "APD(0x20):%s  ON_key:%s  ESC:%s",
                 g_last_res.saw_apd_event ? "OUI" : "NON",
                 g_last_res.saw_on_key ? "OUI" : "NON",
                 g_last_res.ended_by_esc ? "OUI" : "NON");
        snprintf(l4, sizeof(l4), "Verifiez: ecran eteint + reveil ON sans gel");
        draw_text(14, 156, l1, COL_OK);
        draw_text(14, 172, l2, COL_TEXT);
        draw_text(14, 188, l3, g_last_res.saw_apd_event ? COL_OK : COL_WARN);
        draw_text(14, 206, l4, COL_MUTED);
    }

    lcd_blit(g_fb, SCR_320x240_565);
}

/*
 * Test A1 & A2:
 *   use_os_screen = false -> keep SCR_320x240_565 (draw live indicator before blocking)
 *   use_os_screen = true  -> switch back to SCR_TYPE_INVALID + saved OS VRAM first
 * Enables OS interrupts via TCT_Local_Control_Interrupts(0) and waits in get_event().
 * Returns immediately if ESC is pressed (either via OS event or hardware matrix check).
 */
static void run_get_event_probe(bool use_os_screen) {
    memset(&g_last_res, 0, sizeof(g_last_res));
    g_last_res.has_run = true;
    snprintf(g_last_res.mode_name, sizeof(g_last_res.mode_name),
             use_os_screen ? "A2 (OS+get_event)" : "A1 (FB+get_event)");

    if (!use_os_screen) {
        fill_rect(8, 148, 304, 84, COL_CARD);
        draw_box(8, 148, 304, 84, COL_CARD, COL_ACCENT);
        draw_text(14, 158, "A1 ACTIF : Ints(0) + get_event()...", COL_ACCENT);
        draw_text(14, 174, "Ne touchez plus aux touches.", COL_TEXT);
        draw_text(14, 190, "Appuyez sur [ESC] pour arreter A1.", COL_WARN);
        lcd_blit(g_fb, SCR_320x240_565);
    } else {
        lcd_init(SCR_TYPE_INVALID);
        if (has_colors && g_os_vram) {
            void *rv = (void *)REAL_SCREEN_BASE_ADDRESS;
            if (rv) memcpy(rv, g_os_vram, SP_FB_BYTES);
        }
    }

    wait_release_all();
    uint32_t t0 = read_rtc_sec();

    int prev_mask = TCT_Local_Control_Interrupts(0);

    for (unsigned iter = 0; iter < 512U; iter++) {
        struct s_ns_event ev;
        memset(&ev, 0, sizeof(ev));
        int rc = get_event(&ev);
        g_last_res.events_seen++;
        g_last_res.last_ev_type = ev.type;
        g_last_res.last_ev_key = ev.key;
        g_last_res.ret_code = rc;

        if (ev.type == 0x20U) {
            g_last_res.saw_apd_event = true;
        }
        if (on_key_pressed()) {
            g_last_res.saw_on_key = true;
        }
        if (isKeyPressed(KEY_NSPIRE_ESC)) {
            g_last_res.ended_by_esc = true;
            break;
        }
        /* Stop after an APD event + brief settle or once user presses any key after 5s */
        if (g_last_res.saw_apd_event && ev.type == 0x8U) {
            break;
        }
    }

    TCT_Local_Control_Interrupts(prev_mask);
    g_last_res.elapsed_sec = read_rtc_sec() - t0;

    if (use_os_screen) {
        lcd_init(SCR_320x240_565);
    }
    wait_release_all();
}

/*
 * Test A3:
 * Restores OS screen mode (SCR_TYPE_INVALID) and invokes TI-OS's own modal dispatcher
 * via show_msgbox(), which internally runs TCT_Local_Control_Interrupts(0) + show_dialog_box2_().
 * When user dismisses or wakes & dismisses the dialog, restores SCR_320x240_565.
 */
static void run_os_dispatcher_probe(void) {
    memset(&g_last_res, 0, sizeof(g_last_res));
    g_last_res.has_run = true;
    snprintf(g_last_res.mode_name, sizeof(g_last_res.mode_name), "A3 (OS msgbox)");

    lcd_init(SCR_TYPE_INVALID);
    if (has_colors && g_os_vram) {
        void *rv = (void *)REAL_SCREEN_BASE_ADDRESS;
        if (rv) memcpy(rv, g_os_vram, SP_FB_BYTES);
    }
    wait_release_all();

    uint32_t t0 = read_rtc_sec();
    show_msgbox(
        "Sleep Probe - Test A3",
        "Attendez le delai de veille TI-OS sans toucher aux touches.\n"
        "Apres la veille, reveillez avec [ON] puis appuyez sur [ESC] ou [ENTER]."
    );
    g_last_res.elapsed_sec = read_rtc_sec() - t0;
    g_last_res.events_seen = 1U;
    g_last_res.ended_by_esc = isKeyPressed(KEY_NSPIRE_ESC);

    lcd_init(SCR_320x240_565);
    wait_release_all();
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    g_entry_intmask = TCT_Local_Control_Interrupts(-1);
    TCT_Local_Control_Interrupts(g_entry_intmask);

    g_fb = (uint16_t *)calloc(SP_W * SP_H, sizeof(uint16_t));
    if (!g_fb) return 1;

    void *rv = (void *)REAL_SCREEN_BASE_ADDRESS;
    if (has_colors && rv) {
        g_os_vram = malloc(SP_FB_BYTES);
        if (g_os_vram) memcpy(g_os_vram, rv, SP_FB_BYTES);
    }

    if (!lcd_init(SCR_320x240_565)) {
        free(g_os_vram);
        free(g_fb);
        return 1;
    }

    wait_release_all();
    uint32_t start_rtc = read_rtc_sec();
    uint32_t cycles = 0U;
    uint32_t last_drawn_rtc = start_rtc - 1U;

    while (1) {
        uint32_t now_rtc = read_rtc_sec();
        if (now_rtc != last_drawn_rtc) {
            last_drawn_rtc = now_rtc;
            render_ui(start_rtc, cycles);
        }

        if (isKeyPressed(KEY_NSPIRE_ESC)) {
            break;
        }
        if (isKeyPressed(KEY_NSPIRE_1)) {
            cycles++;
            run_get_event_probe(false);
            last_drawn_rtc = 0xFFFFFFFFU;
        } else if (isKeyPressed(KEY_NSPIRE_2)) {
            cycles++;
            run_get_event_probe(true);
            last_drawn_rtc = 0xFFFFFFFFU;
        } else if (isKeyPressed(KEY_NSPIRE_3)) {
            cycles++;
            run_os_dispatcher_probe();
            last_drawn_rtc = 0xFFFFFFFFU;
        }

        safe_delay_ms(30);
    }

    lcd_init(SCR_TYPE_INVALID);
    if (has_colors && g_os_vram) {
        void *out_vram = (void *)REAL_SCREEN_BASE_ADDRESS;
        if (out_vram) memcpy(out_vram, g_os_vram, SP_FB_BYTES);
    }
    free(g_os_vram);
    free(g_fb);
    wait_release_all();
    return 0;
}
