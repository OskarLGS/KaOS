#ifndef LATIN1_H
#define LATIN1_H

#include <stdint.h>

// Text in KaOS is Latin-1 (ISO-8859-1): one byte per character, ASCII
// plus the accented letters and signs Spanish and its neighbours need.
// Latin-1 is also the first 256 code points of Unicode, so FAT32 long
// names store these characters correctly for other systems.

// 8x8 glyph for any byte: ASCII from the font, supported Latin-1
// characters composed from a base letter + accent, '?' otherwise.
const uint8_t* latin1_glyph(uint8_t c);

// Same character in the VGA text-mode code page (CP437), or the
// closest one (an unaccented letter) when CP437 lacks it.
uint8_t latin1_to_cp437(uint8_t c);

// Case folding for comparisons (A-Z and accented capitals)
uint8_t latin1_lower(uint8_t c);

#endif
