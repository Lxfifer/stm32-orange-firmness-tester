# Archived drivers (not compiled)

Nothing in this directory is listed in `MDK-ARM/F103.uvprojx`, so none of it is
compiled or linked into the firmware. It is kept because it documents
alternative hardware and because it can be re-enabled later.

| File | Content | Status |
|---|---|---|
| `scale_module.c`, `scale_module.h` | HAL port of the digital load-cell transmitter module (custom TTL protocol, USART3 on PB10/PB11, hex frames with XOR checksum, baud-rate switching, 640 Hz / averaging-1 configuration) | works, but the current hardware uses the HX711 instead |
| `oled.c`, `oled.h`, `oledfont.h` | 128×64 SSD1306 OLED driver, 4-wire software SPI on PB8/PB9/PB13/PB14, ASCII 6×8 and 8×16 fonts plus a Chinese font table | reference only; `oled.h` still contains two StdPeriph macros (`GPIO_ResetBits`/`GPIO_SetBits`) for an unused test LED, so convert those to HAL calls before adding the file to a build |

These two groups were converted from GBK to UTF-8 and their comments were
rewritten in English, which is why they appear here rather than in the original
working tree (see `docs/07-history.md`).
