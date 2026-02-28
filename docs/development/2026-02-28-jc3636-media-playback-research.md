# JC3636W518 Media Playback Research (MP3 + Video on SD)

Date: 2026-02-28  
Scope: Research only (based on upstream sample code and library docs), no design decision forced yet.

## 1. Goal

Understand how vendor/community demos make MP3 and video playback work on low-memory ESP32 hardware, and extract reusable principles for our project.

## 2. Repositories and files reviewed

### 2.1 Board/sample repos

- `https://github.com/moononournation/JC3636W518`
  - `AviMp3Mjpeg/AviMp3Mjpeg.ino`
  - `AviMp3Mjpeg/AviFunc.h`
  - `AviMp3Mjpeg/esp32_audio.h`
  - `ImgViewerMjpeg_JPEGDEC/ImgViewerMjpeg_JPEGDEC.ino`
  - `ImgViewerMjpeg_JPEGDEC/MjpegFunc.h`
  - `ImgViewerMjpeg_Zoomquilt/MjpegFunc.h`
- `https://github.com/kotborealis/ESP32_Display_Panel_JC3636W518`
  - Checked examples structure and LVGL sample coverage

### 2.2 Dependency repos

- `https://github.com/esp-arduino-libs/ESP32_JPEG`
  - `src/include/esp_jpeg_dec.h`
  - `src/include/esp_jpeg_common.h`
  - `examples/DecodeTest/DecodeTest.ino`
- `https://github.com/lanyou1900/avilib`
  - `avilib.h`
  - `avilib.c`
- `https://github.com/pschatzmann/arduino-libhelix`
  - `README.md`
  - `src/ConfigHelix.h`
  - `src/MP3DecoderHelix.h`

## 3. What their AVI+MP3 demo actually does

## 3.1 Media format is preprocessed, not generic

The demo does not play arbitrary desktop video files directly. It expects:

- Container: AVI
- Video codec: MJPEG
- Audio codec: MP3
- Resolution: square and display-friendly (example uses 360x360)

Sample conversion command from the demo:

```bash
ffmpeg -y -i input.webm -c:a mp3 -c:v mjpeg -q:v 7 \
  -vf "scale=-1:360:flags=lanczos,crop=360:360:(in_w-360)/2:0" \
  AviMp3Mjpeg360sq.avi
```

Implication: decode complexity is moved to offline preprocessing.

## 3.2 Storage and mount mode

In `AviMp3Mjpeg.ino`, they explicitly set SD_MMC pins and mount with:

- 4-bit mode (`mode1bit = false`)
- high-speed SD clock (`SDMMC_FREQ_HIGHSPEED`)
- root path `/root`

This is a throughput-oriented setup for sustained media reads.

## 3.3 Video path (MJPEG in AVI)

Pipeline in `AviFunc.h`:

1. Open AVI and parse indexes/metadata (`AVI_open_input_file`).
2. Read one compressed MJPEG frame at a time (`AVI_read_frame`).
3. Parse JPEG header and decode with `ESP32_JPEG`:
   - `jpeg_dec_parse_header`
   - `jpeg_dec_process`
4. Draw decoded RGB565 buffer directly to display:
   - `gfx->draw16bitBeRGBBitmap(...)`

Important details:

- Output frame buffer is allocated once and 16-byte aligned.
- Decode target is fixed RGB565 (memory and bandwidth friendly).
- No LVGL image widget is used in this path.

## 3.4 Audio path (MP3 in AVI)

Pipeline in `esp32_audio.h`:

1. Read compressed MP3 bytes from AVI incrementally (`AVI_read_audio`).
2. Feed MP3 bytes into `MP3DecoderHelix` via `mp3.write(...)`.
3. Callback receives decoded PCM samples.
4. Push PCM to I2S (`i2s_write`).
5. If MP3 frame sample rate changes, call `i2s_set_clk` dynamically.

Tasking model:

- MP3 decode/playback runs in a dedicated FreeRTOS task pinned to a core.
- Task priority is high (`configMAX_PRIORITIES - 1`).
- Video loop and audio task run concurrently.

## 3.5 Sync strategy

Video loop uses frame deadline scheduling:

- `avi_next_frame_ms = start + frame_index * 1000 / fps`
- If current time is late, skip frame (`avi_skipped_frames++`).
- Audio keeps streaming in parallel.

This is a pragmatic "video-anchored with frame dropping" strategy, not perfect A/V sync with timestamp correction.

## 4. Why this works on low-memory MCU

Core principles observed:

- Stream instead of full-load:
  - one frame/chunk at a time from SD.
- Fixed-size working buffers:
  - compressed video buffer (`estimateBufferSize`).
  - audio buffer (`MP3_MAX_FRAME_SIZE`).
- One reusable output frame buffer (RGB565).
- Offline transcoding:
  - convert to MJPEG+MP3 AVI at compatible resolution.
- Use decoder-friendly formats:
  - avoid runtime conversion complexity.

In short: stable memory footprint beats format flexibility.

## 5. Constraints and caveats from upstream

## 5.1 ESP32_JPEG constraints

From `esp_jpeg_common.h` / `esp_jpeg_dec.h`:

- output buffer must be aligned (16-byte alignment requirement is used in examples).
- error codes include:
  - `JPEG_ERR_MEM` (not enough memory)
  - `JPEG_ERR_FMT2` (format right but not supported)
  - `JPEG_ERR_FMT3` (JPEG standard not supported)

Meaning: even "valid JPEG" may still be unsupported for this decoder path.

## 5.2 MP3 decoder constraints

From `arduino-libhelix`:

- default `MP3_MAX_FRAME_SIZE` is `1024 * 2`.
- extreme bitrates may require increasing frame/output buffer sizes.
- stream should avoid unrelated metadata noise if possible.

## 5.3 Demo quality caveats

Upstream demo is pragmatic and fast, but not production-hardened:

- limited error handling around decoder return values.
- cleanup in `avi_close()` is intentionally minimal in sample form.
- assumes prepared media and known-good pipeline.

## 6. Relevance to our project

For media features on our device, we should treat "photo/video/audio pages" as a dedicated streaming subsystem, not as generic desktop-like file rendering.

Recommended direction when implementing media pages:

1. Define supported input profiles first (resolution, codec, bitrate, container).
2. Add offline conversion scripts as part of workflow.
3. Keep runtime memory fixed and reusable.
4. Use direct draw pipeline for high-throughput media.
5. Keep LVGL for UI controls/overlays, not heavy frame decoding abstraction.

## 7. Practical checklist for next implementation

- SD throughput mode verified (4-bit + high speed if board wiring allows).
- media file preprocessing pipeline scripted (ffmpeg presets).
- frame buffer and audio buffers allocated once, aligned where required.
- decoder return codes logged and surfaced on UI.
- frame skip stats and decode timing metrics exposed for tuning.

## 8. Key takeaway

The "same principle" across image/video/audio on constrained embedded devices is:

- preprocess aggressively,
- stream incrementally,
- reuse fixed buffers,
- prioritize deterministic memory behavior over universal format compatibility.
