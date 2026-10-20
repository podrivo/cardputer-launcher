# Cardputer Mini Launcher

Tiny factory-partition launcher for the [M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv). Flash it once over USB-C from [Cardputer ADV Installers](https://github.com/podrivo/cardputer), then **Add to launcher** packs each app into the free flash (only the bytes it needs, up to 16 apps).

**Power off always returns here.** Opening an app uses deep-sleep + RTC retain so the permanent boot target stays `factory` (works even when catalog/Arduino apps call `esp_ota_mark_app_valid_cancel_rollback()`).

Based on [cardputer-adv-template](https://github.com/podrivo/cardputer-adv-template). Prior art: [bmorcelli/Launcher](https://github.com/bmorcelli/Launcher), [d4rkmen/M5Apps](https://github.com/d4rkmen/M5Apps).

## Keys

| Key | Action |
|-----|--------|
| ← / → (also ↑ / ↓) | Browse installed apps |
| Enter | Open (one-shot boot) |

## Prerequisites

Same as the ADV template: ESP-IDF **v5.4.2**, `esp32s3` target, vendored M5GFX.

```bash
. ~/esp/esp-idf/export.sh
pnpm run deps          # clones M5GFX 0.2.33 if missing
pnpm run setup         # idf.py set-target esp32s3
pnpm run build
pnpm run flash
pnpm run bin           # → bins/cardputer_launcher.bin (merged @ 0x0)
```

## Local packing test

```bash
pnpm run demo-bin                          # → bins/demo.bin (app-only)
pnpm run append -- bins/cardputer_launcher.bin bins/demo.bin
# flash the resulting *-with-demo.bin at 0x0
```

`scripts/append-partition.mjs` mirrors the website: read table → carve next `ota_N` → write app.

## Partition layout

Initial flash: `nvs` / `otadata` / `phy_init` / compact `factory` (this launcher). Rest of 8 MB is free until the installers site appends sized `ota_0`…`ota_15`.

## License

MIT — see [LICENSE](LICENSE).
