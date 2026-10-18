# Real-Time Clock (kernel/rtc.c)

Wall-clock date and time from the CMOS RTC (ports 0x70 index / 0x71 data).

## Reading safely

| Problem | Handling |
|---------|----------|
| The RTC updates its registers once per second | Wait while register A bit 7 (update in progress) is set (bounded loop), then read every field twice until two snapshots match |
| Index/data pair must not be split | Each `cmos_read` saves EFLAGS, `cli`, restores IF |
| BCD vs binary | Register B bit 2 clear = BCD → converted |
| 12-hour mode | Register B bit 1 clear = 12 h; hour bit 7 = PM |
| Century | Register 0x32 if it reads 19-30, else 20xx |
| Weekday register is unreliable | Computed from the date (Sakamoto) |

The time is shown exactly as the RTC stores it. QEMU stores UTC by
default; start it with `-rtc base=localtime` to see local time.

## API (kernel/rtc.h)

| Function | Result |
|----------|--------|
| `rtc_read(&t)` | Fills `rtc_time_t` (year, month, day, hour, minute, second, weekday) |
| `rtc_format_date` / `rtc_format_time` | `"2026-10-18"` / `"09:05:07"` |
| `rtc_weekday_name` | `"Sun"` ... `"Sat"` |
| `rtc_fat_date` / `rtc_fat_time` | FAT directory-entry encodings |

## Users

- Shell `date` → `Sun 2026-10-18 09:05:47`.
- FAT32: new entries get creation/write/access stamps; every write
  updates the write/access stamps (`mdir` on the host shows them).
- Desktop: `HH:MM` at the right of the taskbar and the **Clock** window
  (large `HH:MM:SS` via `draw_string_scaled`, plus weekday and date),
  both refreshed by the once-per-second desktop redraw.
