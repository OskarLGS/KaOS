#ifndef RTC_H
#define RTC_H

#include <stdint.h>

// Wall-clock time from the CMOS real-time clock. The RTC keeps whatever
// the firmware stores (QEMU: UTC unless started with -rtc base=localtime);
// KaOS shows it as is.
typedef struct {
    uint16_t year;     // e.g. 2026
    uint8_t  month;    // 1-12
    uint8_t  day;      // 1-31
    uint8_t  hour;     // 0-23
    uint8_t  minute;   // 0-59
    uint8_t  second;   // 0-59
    uint8_t  weekday;  // 0 = Sunday ... 6 = Saturday (computed, not read)
} rtc_time_t;

void rtc_read(rtc_time_t* t);

// "2026-10-18" (11 bytes) / "09:05:07" (9 bytes) / "Sun" (static string)
void rtc_format_date(const rtc_time_t* t, char* out);
void rtc_format_time(const rtc_time_t* t, char* out);
const char* rtc_weekday_name(const rtc_time_t* t);

// FAT directory-entry encodings (date: years since 1980; time: 2 s units)
uint16_t rtc_fat_date(const rtc_time_t* t);
uint16_t rtc_fat_time(const rtc_time_t* t);

#endif
