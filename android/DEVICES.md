# Devices tested

One row per phone that ran the [5-minute check](INSTALL.md#5-minute-check) and sent its diagnostic report (#89, #91). The app targets arm64-v8a phones at API 29 (Android 10) or later; the Pixel 8a is the reference phone, where `TESTING.md` sets tighter targets.

## The floor

Measured with the phone cool (Thermal Status 0), not charging, in normal room light, at the default `min patch area` of 32 px²:

| Measure | Floor | In the report |
|---|---|---|
| Preview | median ≥ 15 fps over 60 s | `preview.fps_median` (over `preview.seconds`, which must be 60) |
| Photo → Result | median ≤ 2.5 s over five shutter Photos at 2048 px on the long side | `photos.median_ms`, with `photo.1` … `photo.5` all `camera` and about 2048 on the long side |
| Memory | no crash, OOM or low-memory kill during the check | `crash.last` and `exit.last` (API 30+): `none`, or not from the check's time |

Below the floor the app runs as usual, with no warning and no block: "unsupported" means no promise and no speed fixes for that phone. The memory figures in MB stay a Pixel-only check (`TESTING.md`, Performance).

## Verdicts

- **Supported:** every floor is met, nothing crashed, and every step of the check worked.
- **Supported with reservations:** every floor is met and nothing crashed, but a step failed: a functional defect, recorded in its own issue and linked from the phone's issue.
- **Unsupported:** a floor is missed, or the app crashed or was killed.

The report gives the numbers, not the verdict: the developer judges, from the report and the failed steps the tester sent. Every failed step opens an issue. A report taken with a `thermal.status` other than `none (0)`, or with fewer than 60 `preview.seconds`, is not a fair measurement: ask for the check again before judging the floor.

## Recording a phone

1. Open an issue with the "Tested phone" template (`.github/ISSUE_TEMPLATE/tested-phone.yml`): paste the report and the failed steps.
2. Add a row below. Model is `device.manufacturer` and `device.model`, SoC `device.soc`, API `device.api`; fps is `preview.fps_median`, the Photo time `photos.median_ms` in seconds; the hash matches when `core.hash_fingers` ends in `(golden)`. The date is the report's (`report.time`).
3. A hash that isn't golden is not a verdict by itself, but it means the core computes other floats on that SoC: open an issue.

When the phone passes through the developer's hands, add logcat and `android/scripts/core-on-device.sh` over adb, with USB debugging on for that session only.

## Phones

| Model | SoC | API | Median fps | Median Photo | Hash | Verdict | Date | Issue |
|---|---|---|---|---|---|---|---|---|
| Google Pixel 8a | Tensor G3 | 37 | _to fill_ | _to fill_ | _to fill_ | _to fill_ | _to fill_ | _to fill_ |

The Pixel 8a's row waits for its first diagnostic report from the 5-minute check. Its earlier measurements over adb (#64, #76: 27–31 fps, Photos in 0.93–1.10 s) came from logcat, not from a report.
