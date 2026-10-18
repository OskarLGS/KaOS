#include "latin1.h"
#include "font.h"

// Accent marks, as rows drawn above a lowercase letter (rows 0-1, which
// this font leaves empty for lowercase) or squeezed into one row above a
// capital (the capital moves down a row).
enum { NONE, GRAVE, ACUTE, CIRC, DIAER, TILDE, CEDIL };

static const uint8_t accent_rows[][2] = {
    [GRAVE] = { 0x30, 0x18 },
    [ACUTE] = { 0x0C, 0x18 },
    [CIRC]  = { 0x18, 0x24 },
    [DIAER] = { 0x00, 0x66 },
    [TILDE] = { 0x34, 0x58 },
};
static const uint8_t accent_one_row[] = {
    [GRAVE] = 0x30, [ACUTE] = 0x0C, [CIRC] = 0x3C, [DIAER] = 0x66, [TILDE] = 0x3C,
};

// Composed characters: Latin-1 code -> base letter + accent
static const struct { uint8_t code; char base; uint8_t accent; } composed[] = {
    { 0xC0, 'A', GRAVE }, { 0xC1, 'A', ACUTE }, { 0xC2, 'A', CIRC }, { 0xC4, 'A', DIAER },
    { 0xC7, 'C', CEDIL }, { 0xC8, 'E', GRAVE }, { 0xC9, 'E', ACUTE }, { 0xCA, 'E', CIRC },
    { 0xCB, 'E', DIAER }, { 0xCC, 'I', GRAVE }, { 0xCD, 'I', ACUTE }, { 0xCE, 'I', CIRC },
    { 0xCF, 'I', DIAER }, { 0xD1, 'N', TILDE }, { 0xD2, 'O', GRAVE }, { 0xD3, 'O', ACUTE },
    { 0xD4, 'O', CIRC },  { 0xD6, 'O', DIAER }, { 0xD9, 'U', GRAVE }, { 0xDA, 'U', ACUTE },
    { 0xDB, 'U', CIRC },  { 0xDC, 'U', DIAER },
    { 0xE0, 'a', GRAVE }, { 0xE1, 'a', ACUTE }, { 0xE2, 'a', CIRC }, { 0xE4, 'a', DIAER },
    { 0xE7, 'c', CEDIL }, { 0xE8, 'e', GRAVE }, { 0xE9, 'e', ACUTE }, { 0xEA, 'e', CIRC },
    { 0xEB, 'e', DIAER }, { 0xEC, 'i', GRAVE }, { 0xED, 'i', ACUTE }, { 0xEE, 'i', CIRC },
    { 0xEF, 'i', DIAER }, { 0xF1, 'n', TILDE }, { 0xF2, 'o', GRAVE }, { 0xF3, 'o', ACUTE },
    { 0xF4, 'o', CIRC },  { 0xF6, 'o', DIAER }, { 0xF9, 'u', GRAVE }, { 0xFA, 'u', ACUTE },
    { 0xFB, 'u', CIRC },  { 0xFC, 'u', DIAER },
};

// Signs drawn directly
static const struct { uint8_t code; uint8_t rows[8]; } signs[] = {
    { 0xA1, { 0x00, 0x18, 0x00, 0x18, 0x18, 0x3C, 0x3C, 0x18 } },  // ¡
    { 0xBF, { 0x00, 0x18, 0x00, 0x18, 0x30, 0x66, 0x66, 0x3C } },  // ¿
    { 0xAA, { 0x38, 0x0C, 0x3C, 0x6C, 0x3C, 0x00, 0x7E, 0x00 } },  // ª
    { 0xBA, { 0x38, 0x6C, 0x6C, 0x6C, 0x38, 0x00, 0x7C, 0x00 } },  // º
    { 0xB7, { 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00 } },  // ·
    { 0xAC, { 0x00, 0x00, 0x00, 0x7E, 0x06, 0x06, 0x00, 0x00 } },  // ¬
    { 0xB4, { 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },  // ´
    { 0xA8, { 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },  // ¨
};

static uint8_t glyphs[128][8];   // Built on first use for 0x80-0xFF
static int built = 0;

static void build() {
    for (int i = 0; i < 128; i++)
        for (int r = 0; r < 8; r++)
            glyphs[i][r] = font_bitmap['?'][r];

    for (unsigned i = 0; i < sizeof(composed) / sizeof(composed[0]); i++) {
        uint8_t* g = glyphs[composed[i].code - 0x80];
        const uint8_t* b = font_bitmap[(uint8_t)composed[i].base];
        int acc = composed[i].accent;
        int lower = composed[i].base >= 'a';

        if (acc == CEDIL) {                 // Hook in the empty bottom row
            for (int r = 0; r < 8; r++) g[r] = b[r];
            g[7] = 0x18;
        } else if (lower) {                 // Accent in the empty top rows
            for (int r = 0; r < 8; r++) g[r] = b[r];
            g[0] = accent_rows[acc][0];
            g[1] = accent_rows[acc][1];
        } else {                            // Capital moves down a row
            g[0] = accent_one_row[acc];
            for (int r = 1; r < 8; r++) g[r] = b[r - 1];
        }
    }
    for (unsigned i = 0; i < sizeof(signs) / sizeof(signs[0]); i++)
        for (int r = 0; r < 8; r++)
            glyphs[signs[i].code - 0x80][r] = signs[i].rows[r];
    built = 1;
}

const uint8_t* latin1_glyph(uint8_t c) {
    if (c < 0x80) return font_bitmap[(c >= 32 && c <= 126) ? c : '?'];
    if (!built) build();
    return glyphs[c - 0x80];
}

uint8_t latin1_to_cp437(uint8_t c) {
    if (c < 0x80) return c;
    static const uint8_t direct[][2] = {
        { 0xC7, 0x80 }, { 0xFC, 0x81 }, { 0xE9, 0x82 }, { 0xE2, 0x83 }, { 0xE4, 0x84 },
        { 0xE0, 0x85 }, { 0xE7, 0x87 }, { 0xEA, 0x88 }, { 0xEB, 0x89 }, { 0xE8, 0x8A },
        { 0xEF, 0x8B }, { 0xEE, 0x8C }, { 0xEC, 0x8D }, { 0xC4, 0x8E }, { 0xC9, 0x90 },
        { 0xF4, 0x93 }, { 0xF6, 0x94 }, { 0xF2, 0x95 }, { 0xFB, 0x96 }, { 0xF9, 0x97 },
        { 0xD6, 0x99 }, { 0xDC, 0x9A }, { 0xE1, 0xA0 }, { 0xED, 0xA1 }, { 0xF3, 0xA2 },
        { 0xFA, 0xA3 }, { 0xF1, 0xA4 }, { 0xD1, 0xA5 }, { 0xAA, 0xA6 }, { 0xBA, 0xA7 },
        { 0xBF, 0xA8 }, { 0xAC, 0xAA }, { 0xA1, 0xAD }, { 0xB7, 0xFA },
    };
    for (unsigned i = 0; i < sizeof(direct) / sizeof(direct[0]); i++)
        if (direct[i][0] == c) return direct[i][1];
    // Not in CP437: show the unaccented letter
    for (unsigned i = 0; i < sizeof(composed) / sizeof(composed[0]); i++)
        if (composed[i].code == c) return (uint8_t)composed[i].base;
    return '?';
}

uint8_t latin1_lower(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;   // À-Þ except ×
    return c;
}
