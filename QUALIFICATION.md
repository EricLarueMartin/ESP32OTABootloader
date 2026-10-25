# Power-cycle recovery extension (0.2.0-dev)

The qualification below describes the previously published baseline. The extension adds a persistent interrupted-startup gesture, a 10-second normal cold-start window, a 15-minute emergency window, and a shared authenticated `/update` page. The new recovery passed early-crash and watchdog return with automatic startup disabled, and the new good application was restored. Hardware RTS/EN testing passed double-reset entry (898523 ms remaining when sampled), countdown, wrong-password refusal, persistent authenticated Hold, normal cold-reset startup and software handoff. See [extension evidence](evidence/power-recovery.json). The owner then completed a physical double-power cycle. Before any software mutation, recovery reported emergency mode 2 with 878704 ms remaining and the exact new application SHA-256. Authenticated maintenance claimed indefinite Hold, verified launch restored the good app, and its home link and `/update` page were checked. This extension passed HUZZAH32 acceptance; C6 remains compile-only.

Native source tests pass 10,846 checks, including all marker values, non-cold/non-Healthy fail-safe selection, both exact deadlines, Wi-Fi disconnect/reconnect and unsigned clock wraparound. Five Python tests pass. `node test_web.cjs` exercises the actual embedded JavaScript's application entry, authenticated maintenance claim, manifest installation, credential isolation and authentication failure handling. The deployed recovery page renders correctly in a browser and rejects an empty password with HTTP 401.

Current build sizes: HUZZAH32 recovery 955,568 bytes (93,008 bytes spare), XIAO C6 recovery 1,015,248 bytes (33,328 bytes spare; compile only). The new behavior adds 4,656 bytes and 8 static RAM bytes on HUZZAH32. The C6 remains 12,128 bytes smaller than the original pilot. No live blind migration is included.

Exact ROM readback matches the final recovery (955568 bytes) and good application (926368 bytes) binaries. The owner-confirmed full-power double-cycle test passed. The 15-minute expiration is validated with the production timing helper and a virtual clock; do not claim a full hardware wall-clock soak unless separately recorded.

---

# Qualification — October 25, 2026

HUZZAH32 functional and owner-confirmed physical cold-power tests passed. After the owner disconnected and restored all board power, authenticated status showed the good main application running automatically with recovery selected as its fallback; no software launch command was sent. C6 is compile-checked only; the installed blind pilot is unchanged.

## Results

- Owner-confirmed full power disconnect/reconnect followed by authenticated automatic good-main startup passed; see `evidence/cold-power.json`.

- 10,054 native checks: journal torn writes/CRC/generation wrap, chunk bounds, device/release labels, strict HTTP framing and flash readback corruption.

- Five Python tests: exact immutable range source, malformed ranges, credential-safe redirect refusal, image-manifest binding and credential generation/non-overwrite/path guards.

- Real HUZZAH32: wrong password rejected; interrupted download retained exactly 16,384 bytes across software restart and resumed at that offset; final image SHA and release metadata matched.

- Corrupted image rejected after complete transfer; launch returned HTTP409; authenticated recovery remained available. Good application restored and software return verified.

- Early crash returned to stable recovery; UART shows launch and immediate application reset. Post-Healthy loop hang triggered the task watchdog and returned to stable recovery. See the UART evidence in `evidence/`.

- ROM readback after testing matches the exact expected bootloader, partition table, recovery binary and restored normal selftest image. Image hashes and source hashes are in `evidence/qualification.json`.

- Recovery credential hash and netid survived; the plaintext device password is absent from the configuration partition. Default NVS was not erased but is not byte-identical: PHY calibration/version and three bytes of the legacy Wi-Fi SSID blob changed. All other existing logical records match after CRC-checked blob-format normalization. Raw records/values remain private.

## Measured size

| Target/profile | Binary bytes | Recovery slot spare |

|---|---:|---:|

| HUZZAH32 recovery | 950,912 | 97,664 |

| XIAO C6 recovery (compile only) | 1,010,288 | 38,288 |

| HUZZAH32 normal selftest | 922,704 | Uses 2.75 MiB main slot |

The C6 image is 17,088 bytes smaller than the original 1,027,376-byte pilot while adding identity and metadata. The range-only reader saves 31,520 bytes on HUZZAH32 compared with the preceding 982,432-byte general-HTTP candidate (including later retry changes). Readback scratch saves 3,840 static bytes. HUZZAH32 final static globals are 50,072 bytes; normal selftest reported approximately 197 KiB free heap. These are measured build/bench values, not promises for other applications.

## Development failures and scope

Earlier candidates stopped safely at 163,840/286,720 bytes on source connection failures, then at 835,584 bytes on an incomplete/invalid response. Checkpoints and recovery remained intact. Final source adds bounded connection/incomplete-read retries with backoff and an immutable in-memory host source. The complete interrupted/corrupt-update run and crash/watchdog tests passed after these changes. Malformed headers remain fatal; retrying never erases or writes until a complete valid range has arrived.

Only the original Feather HUZZAH32 was physically qualified through the listed tests. The C6 source builds within its slot but needs its own acceptance before migration. Other chips, SDK releases, flash sizes/security settings and actuator applications remain unsupported until separately reviewed and tested. No motor or blind controller command was sent.

Private rollback images, Wi-Fi configuration and device passwords are outside this Git repository. The `bedroom-side-blinds` credential is staged only; the existing blind's independent credential remains authoritative.
