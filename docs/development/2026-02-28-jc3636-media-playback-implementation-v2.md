# JC3636 Media Playback Implementation Draft v2 (Project-Mapped)

Date: 2026-02-28
Status: Draft for team handover and next implementation sprint

## 1. Why this v2 exists

The first document focused on upstream research and low-memory principles.
This v2 maps those findings to the **current repository reality** so another developer can continue without rediscovery.

## 2. Current project baseline (confirmed)

Codebase state (firmware):
- File: `esp32-firmware/src/main.cpp` (monolithic, single-file architecture)
- UI pages include `UI_PAGE_PHOTO_FRAME` (`UI_PAGE_COUNT = 9`)
- Main runtime loop is cooperative:
  - `webSocket.loop()`
  - periodic network and UI updates
  - `lv_timer_handler()`
- SD stack already exists:
  - card detection and mount fallback (4-bit then 1-bit)
  - SD metadata and root scan diagnostics
  - photo list scan via `loadSdPhotoList(...)`
- Photo page already has UI controls and status text (`Prev`, `Reload`, `Next`), but rendering still has unresolved blank-image behavior on real hardware.

Interpretation:
- We already have page shell + SD plumbing.
- The missing part is a robust media decode/render pipeline aligned with board demo behavior.

## 3. Non-negotiable constraints for this board class

- RAM is limited; stable playback requires fixed buffers and streaming.
- Generic "play anything" is not realistic; input profile must be constrained.
- For video/audio, upstream path expects:
  - AVI container
  - MJPEG video
  - MP3 audio
- For still images, decoder compatibility matters (progressive/unsupported JPEG variants can decode to blank/fail).

## 4. Recommended target architecture (incremental, low-risk)

Keep the current single-file app running first; introduce **logical modules** in stages.
Do not do a risky "big refactor" before media path is stable.

### 4.1 Logical module boundaries

1. `media_storage`
- SD mount state
- file scan/index
- extension and profile filtering

2. `media_photo`
- still image load/decode
- viewport fit/center math
- next/prev navigation and error codes

3. `media_audio`
- MP3 stream read and decode (Helix)
- PCM output to I2S
- dedicated audio task + ring buffer

4. `media_video`
- AVI demux (video/audio chunks)
- MJPEG decode (`ESP32_JPEG`)
- frame timing and skip policy

5. `media_ui_bridge`
- convert media state to LVGL labels/buttons/progress
- input events -> media actions
- avoid heavy decode work inside LVGL callback path

### 4.2 Physical file split (after path is validated)

Phase-A (fastest): keep in `main.cpp`, but isolate by function blocks and clear prefixes.
Phase-B (handover-ready): split into:
- `esp32-firmware/src/media/media_storage.cpp`
- `esp32-firmware/src/media/media_photo.cpp`
- `esp32-firmware/src/media/media_audio.cpp`
- `esp32-firmware/src/media/media_video.cpp`
- `esp32-firmware/src/media/media_ui_bridge.cpp`
- `esp32-firmware/include/media/*.h`

## 5. Runtime/tasking model

Minimum stable model:

- Loop thread (existing):
  - LVGL tick/render
  - websocket
  - lightweight state update

- Audio task (new, pinned, high priority):
  - decode MP3 -> PCM
  - write PCM to I2S continuously

- Video path:
  - start with loop-driven single-thread frame scheduling
  - if frame starvation appears, move decode to a medium-priority task

Rules:
- UI callbacks only enqueue actions/state; no blocking SD reads inside button callbacks.
- SD read and decode use bounded chunk sizes.
- Always allow graceful stop when leaving media page.

## 6. Data/buffer policy (must stay deterministic)

Suggested initial budget (adjust by logs):

- JPEG compressed input buffer: 256-512 KB max
- One RGB565 frame buffer (360x360): ~259 KB
- MP3 input chunk: 2-8 KB
- PCM ring buffer: 16-32 KB
- AVI read buffer: fixed-size, reused

Rules:
- Allocate once, reuse, free on media page exit.
- Prefer PSRAM-backed allocations for large buffers when available.
- Enforce hard file size and dimension guards before decode.

## 7. Supported media profile (v1 contract)

Still image:
- `.jpg`, `.jpeg`, `.sjpg`
- baseline JPEG preferred
- recommended max dimension: <= 1024 on long edge for reliability

Video:
- `.avi`
- video: MJPEG
- audio: MP3
- 360x360 recommended (match panel)
- fps target: 12-20 for stable decode margin

Audio:
- `.mp3` (CBR 96-192 kbps recommended)

## 8. Preprocess pipeline (host-side)

### 8.1 Video convert preset

```bash
ffmpeg -y -i input.mp4 -c:a mp3 -b:a 128k -ar 44100 -ac 2 \
  -c:v mjpeg -q:v 7 -vf "scale=-1:360:flags=lanczos,crop=360:360" \
  output_360_mjpeg_mp3.avi
```

### 8.2 Image normalize preset

```bash
ffmpeg -y -i input.jpg -vf "scale='min(1024,iw)':-1" -q:v 3 output.jpg
```

Goal: shift complexity to preprocessing; keep runtime simple and bounded.

## 9. Why current photo can be blank (likely causes)

Observed symptom in this project: image area centered correctly but content appears blank.

Most likely causes:
1. LVGL decoder path accepts source but fails to decode that JPEG variant.
2. Image payload is read but metadata/pixel data path is incompatible with active decoder backend.
3. Large/progressive/unsupported JPEG causes silent fallback.
4. Object clipping and draw path are valid, but bitmap data is invalid/empty post decode.

Recommended debug order:
1. Validate with known-good baseline JPEG generated by ffmpeg.
2. Add decode-result logging (header parse, decode return code, width/height).
3. Temporarily replace LVGL raw route with `ESP32_JPEG` direct decode + direct draw for A/B comparison.
4. If direct decode works and LVGL path fails, keep LVGL for controls only and use direct draw for media viewport.

## 10. Milestone plan (implementation sequence)

M1: Photo reliability baseline
- Use a strict known-good JPEG set from SD
- Add full decode logs and hard failure reasons on UI
- Exit criteria: 20+ image next/prev cycles without blank/freeze

M2: MP3 standalone playback
- Read MP3 from SD
- Helix decode + I2S output task
- Add pause/stop and page-exit cleanup
- Exit criteria: >= 10 min continuous playback, no watchdog/reset

M3: AVI MJPEG video-only
- AVI demux + MJPEG frame decode
- Frame schedule + skip stats
- Exit criteria: stable 360x360 playback with bounded frame drops

M4: AVI MJPEG + MP3 sync
- Parallel audio task + video loop/task
- Coarse sync by frame deadlines and skip strategy
- Exit criteria: no hard stutter, no drift explosion during 5 min test

M5: Productize media page UX
- mode tabs (Photo / Music / Video)
- robust button hit areas for round screen
- readable status and error text

M6: Codebase cleanup for handover
- split modules to `src/media/*`
- document APIs and state machine
- add troubleshooting doc for field debugging

## 11. Acceptance checklist for teammate handover

Functional:
- SD mount status shown correctly
- photo prev/next/reload works on device
- MP3 play/pause/stop works
- AVI sample plays with acceptable smoothness

Stability:
- no UI deadlock after rapid swipe + button spam
- no memory growth across repeated open/close media page
- no watchdog reset over 30 min mixed usage

Observability:
- serial logs include decode timings and failure reason
- page status label reflects actionable state (loading/unsupported/decode fail)

## 12. Immediate next step (pragmatic)

Do **M1 only** first:
- lock a known-good JPEG sample set,
- prove deterministic decode path on this hardware,
- then branch to MP3/video.

Reason: photo decode stability is the foundation for all richer media features.

