#include "rtc.h"
#include "ports.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

#define REG_SECONDS 0x00
#define REG_MINUTES 0x02
#define REG_HOURS   0x04
#define REG_DAY     0x07
#define REG_MONTH   0x08
#define REG_YEAR    0x09
#define REG_CENTURY 0x32   // Not standard, but QEMU/most PCs provide it
#define REG_A       0x0A   // bit 7: update in progress
#define REG_B       0x0B   // bit 2: binary (else BCD), bit 1: 24-hour

// Index + data must not be split by an interrupt that also touches the
// CMOS, so each access runs with interrupts off (restoring IF after).
static uint8_t cmos_read(uint8_t reg) {
    uint32_t eflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(eflags) :: "memory");
    port_byte_out(CMOS_INDEX, reg);
    uint8_t v = port_byte_in(CMOS_DATA);
    if (eflags & 0x200) __asm__ __volatile__("sti" ::: "memory");
    return v;
}

static int update_in_progress() {
    return cmos_read(REG_A) & 0x80;
}

typedef struct { uint8_t s, m, h, d, mo, y, c; } raw_t;

static void read_raw(raw_t* r) {
    // An update takes ~2 ms; bound the wait so a missing RTC can't hang us
    for (int i = 0; i < 100000 && update_in_progress(); i++) ;
    r->s  = cmos_read(REG_SECONDS);
    r->m  = cmos_read(REG_MINUTES);
    r->h  = cmos_read(REG_HOURS);
    r->d  = cmos_read(REG_DAY);
    r->mo = cmos_read(REG_MONTH);
    r->y  = cmos_read(REG_YEAR);
    r->c  = cmos_read(REG_CENTURY);
}

static uint8_t bcd(uint8_t v) {
    return (v & 0x0F) + (v >> 4) * 10;
}

// Sakamoto's algorithm: 0 = Sunday
static uint8_t day_of_week(int y, int m, int d) {
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
}

void rtc_read(rtc_time_t* t) {
    // Read until two consecutive snapshots agree, so we never combine
    // fields from either side of a once-per-second update.
    raw_t a, b;
    read_raw(&b);
    for (int tries = 0; tries < 5; tries++) {
        a = b;
        read_raw(&b);
        if (a.s == b.s && a.m == b.m && a.h == b.h && a.d == b.d &&
            a.mo == b.mo && a.y == b.y && a.c == b.c) break;
    }

    uint8_t regb = cmos_read(REG_B);
    int pm = b.h & 0x80;           // 12-hour mode flags PM in bit 7
    b.h &= 0x7F;

    if (!(regb & 0x04)) {           // BCD encoded
        b.s = bcd(b.s); b.m = bcd(b.m); b.h = bcd(b.h);
        b.d = bcd(b.d); b.mo = bcd(b.mo); b.y = bcd(b.y); b.c = bcd(b.c);
    }
    if (!(regb & 0x02)) {           // 12-hour clock: 12 AM = 0, 12 PM = 12
        b.h %= 12;
        if (pm) b.h += 12;
    }

    // Trust the century register only when it looks like one
    int century = (b.c >= 19 && b.c <= 30) ? b.c : 20;

    t->year    = (uint16_t)(century * 100 + b.y);
    t->month   = b.mo;
    t->day     = b.d;
    t->hour    = b.h;
    t->minute  = b.m;
    t->second  = b.s;
    t->weekday = (b.mo >= 1 && b.mo <= 12) ? day_of_week(t->year, b.mo, b.d) : 0;
}

static void two_digits(char* out, int v) {
    out[0] = '0' + (v / 10) % 10;
    out[1] = '0' + v % 10;
}

void rtc_format_date(const rtc_time_t* t, char* out) {
    two_digits(out, t->year / 100);
    two_digits(out + 2, t->year % 100);
    out[4] = '-';
    two_digits(out + 5, t->month);
    out[7] = '-';
    two_digits(out + 8, t->day);
    out[10] = '\0';
}

void rtc_format_time(const rtc_time_t* t, char* out) {
    two_digits(out, t->hour);
    out[2] = ':';
    two_digits(out + 3, t->minute);
    out[5] = ':';
    two_digits(out + 6, t->second);
    out[8] = '\0';
}

const char* rtc_weekday_name(const rtc_time_t* t) {
    static const char* names[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return names[t->weekday % 7];
}

uint16_t rtc_fat_date(const rtc_time_t* t) {
    int y = t->year < 1980 ? 0 : t->year - 1980;
    return (uint16_t)((y << 9) | (t->month << 5) | t->day);
}

uint16_t rtc_fat_time(const rtc_time_t* t) {
    return (uint16_t)((t->hour << 11) | (t->minute << 5) | (t->second / 2));
}
