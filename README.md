# ESP32 OTA Bootloader

Permanent network recovery application plus one replaceable application. The Espressif bootloader remains the hardware bootloader. Ordinary OTA never writes the recovery image or partition table.

## Status

The published baseline HUZZAH32 bench profile passed interrupted/corrupt OTA, early-crash, watchdog, credential, exact flash-readback and owner-confirmed cold-power/automatic-start tests on October 25, 2026. The power-cycle recovery extension separately passed HUZZAH32 crash/watchdog, reset-gesture, authenticated hold, flash readback and owner-confirmed physical double-power-cycle tests. See [qualification evidence](QUALIFICATION.md). C6 is compile-checked only; the existing blind pilot remains unchanged.

## Device identity and credentials

Provision a lowercase DNS-style `netid` (1–63 characters); the bedroom side blind profile is `bedroom-side-blinds`. The bench profile is `huzzah32-test`. Identity and Wi-Fi settings live in the independent `recovery_cfg` partition and survive application updates. DHCP hostname uses netid; this is not an mDNS responder.

Generate one random 256-bit password per device with `python credentials.py --directory PRIVATE_DIRECTORY NETID`. The private JSON contains the password; firmware receives only its SHA-256 hash. Generation refuses to overwrite an existing file or place passwords inside this repository. Back up passwords privately. Do not rotate the stable recovery password with application releases.

Initial serial provisioning accepts `RECOVERYCFG <base64-ssid> <base64-wifi-password> <sha256-password> <netid>` only while unconfigured. Send this from a private in-process provisioner, never through shell arguments or logs. The hash is committed last. An already provisioned pilot without netid requires a separate reviewed migration; this candidate does not silently rewrite its configuration.

Authentication uses a constant-time fixed-length hash comparison. Transport is trusted-LAN HTTP: password hashing at rest does not encrypt bearer credentials on the network, provide secure boot, or sign firmware. Use a protected LAN; TLS/signed-image deployments need a separately qualified profile.

## Recovery contract

`/v1/begin` commits exact image length/SHA-256 before erasure. `/v1/download` pulls exact 4096-byte HTTP ranges, rejects redirects/encoding/range mismatches, verifies flash readback, and checkpoints each completed sector in an alternating two-sector CRC journal. A restart resumes the same digest at the durable boundary. Whole-image SHA-256 and Espressif image verification precede Ready; launch records Trial first. Early startup selects recovery before normal setup. The application marks Healthy only after safe outputs, watchdog and authenticated maintenance are ready. Fault/software returns stay in recovery. Healthy cold power-on keeps recovery available for ten seconds before verifying and launching main. Cut power again during that startup window and restore it to enter a 15-minute recovery window. Allow the first startup about two seconds to initialize before cutting power. The window starts when Wi-Fi connects and restarts after Wi-Fi reconnects. An authenticated maintenance session holds recovery open indefinitely; a public page visit or status read alone does not.

The persistent gesture detects interrupted startup, not elapsed time while unpowered: without a battery-backed clock, a later reconnection after an interrupted startup also enters recovery. This fails toward recovery. Interrupted/incomplete images and fault/software returns never use the automatic timeout. Each normal cold start adds two small NVS marker commits; no timer writes occur in the main application. Marker write/readback failure prevents automatic launch.

Authenticated `/v1/status` includes netid, transaction phase, image length/SHA-256, an explicit release name/version bound to that digest, and the SDK application descriptor (project, version, build date/time, IDF and ELF checksum). Arduino descriptors identify the library build, so they are not treated as release names. During installation metadata is null and the checksum identifies the intended candidate, not a verified running image. The SDK descriptor resides in the checksum-bound firmware; the transaction digest persists independently in the journal. Release metadata is committed as one NVS value before the journal and is exposed only when its digest matches. A reset between commits can omit metadata but cannot assign stale metadata to a different image. Build outputs include a `.manifest.json`; pass it with `client.py --manifest FILE` when downloading. Without labels, status uses `firmware` and a digest prefix.

`/v1/maintenance`, `/v1/restart`, `/v1/hold`, `/v1/abandon`, `/v1/launch` require authentication. Application `/v1/recover` returns to recovery after the application safely quiesces its own hardware. No motor operations are included here. Reconcile uncertain mutations by reading status; never blindly retry flash operations.

## Build and test

Arduino ESP32 3.3.5 / bundled IDF v5.5.1-931-g9bb7aa84fe, 4 MiB flash candidate profiles: Adafruit Feather HUZZAH32 (`huzzah32`) and Seeed XIAO C6 (`xiao-c6`). Other boards, SDK versions, flash/security settings and partitions require separate review. ESP32 bootloader offset is 0x1000; C6 is 0x0. Never reuse a merged image across targets.

```
python build.py recovery --board huzzah32 --config ARDUINO_CONFIG --sdk ARDUINO_IDF_LIBS --cli arduino-cli
python build.py selftest --board huzzah32 --config ARDUINO_CONFIG --sdk ARDUINO_IDF_LIBS --cli arduino-cli
python validate.py
python -m unittest discover -p "test_*.py"
node test_web.cjs
```

`badboot` and `badloop` are deliberately failing qualification profiles. Never leave either installed as normal firmware. Build outputs and generated sketch headers remain under ignored `build/`; originals are not rewritten during compilation. The build supplies target-specific multilib paths itself and does not require editing shared Arduino platform files. Native validation currently uses the documented Windows Visual Studio 2022/MSVC and Windows SDK installation in `validate.py`.

The 4 MiB partition profile preserves NVS at 0x9000, reserves 1 MiB recovery and 2.75 MiB main, and separates update journal and recovery configuration. Actual image size must fit; unsupported/oversized builds fail. Pin and inspect the startup wrapper: stock Arduino auto-erases some NVS failures and auto-confirms OTA, which this architecture intentionally prevents.

Refactor: a bounded range-only HTTP reader replaces the general HTTPClient dependency. Incomplete transport reads/connects get at most three retries after the first attempt, with a 500 ms backoff and main-loop/watchdog service between attempts. Malformed framing is refused immediately. Each attempt completes before any flash erase/write. The host range source snapshots its one image in memory. Readback scratch is 256 bytes instead of a second 4096-byte sector, saving 3840 bytes static RAM with every written byte still checked. Authentication is shared, metadata uses the existing ESP descriptor, and the 60-byte journal ABI is retained. Flash-size measurements belong in qualification evidence; no unmeasured reduction is claimed.

## Hardware acceptance gate

Identify chip/MAC, flash size/security, existing partitions and power state before USB flashing. Preserve an existing test-board image privately where required; no secrets/raw NVS enter this repository. Provision the dedicated bench device, verify wrong credentials are rejected, and record normal update/launch/return, interrupted download + restart/resume, corrupted image + launch refusal, early crash, post-Healthy watchdog hang, cold power-on and unchanged recovery-image readback. Restore the good test image at completion.

`qualify_download.py --key-file PRIVATE_JSON --image SELFTEST_BIN --output RECEIPT_DIRECTORY --bench-no-actuators` exercises interruption, corruption and good-image restoration only on netid `huzzah32-test`. It does not replace cold-boot/watchdog/USB-integrity checks. Commit and publish only after the owner's hardware acceptance gate is met.

## Bench installation and fault tests

Install the CP210x driver for the original HUZZAH32 (Arduino board packages alone do not guarantee a COM port). Inspect the actual chip, flash/security state and original partitions. `install_bench.py` requires a completed private 4 MiB backup, exact MAC and `--stable-usb-bench`; it refuses enabled/unverified secure boot or flash encryption, preserves default NVS, bounds every region and retains an exclusive intent file so an uncertain write cannot be blindly repeated. Use `--esptool` for the pinned tool executable, whose directory also contains `espefuse`.

`provision.py` requires the expected MAC, private device key JSON and private Wi-Fi JSON (`ssid`, `password`); it checks authenticated netid before marking the record provisioned. Do not pass secrets as command-line values.

After interrupted/corrupt-update qualification, run `qualify_faults.py --key-file PRIVATE_JSON --port COM_PORT --output NEW_RECEIPT_DIRECTORY --bench-no-actuators`. It captures UART evidence for early crash and watchdog return, checks stable recovery, and restores the good application. Physical cold-power and ROM readback verification remain separate checks. Never open a second serial monitor during a serial operation.

Hardware references: [Adafruit board setup](https://learn.adafruit.com/adafruit-huzzah32-esp32-feather/using-with-arduino-ide) and [Silicon Labs CP210x drivers](https://www.silabs.com/software-and-tools/usb-to-uart-bridge-vcp-drivers).

## Firmware update link and emergency gesture

Link installed firmware to `/update`. Copy `update_page.h` into the application's sketch and call `registerUpdatePage(api)` before `api.begin()`. Keep its authenticated `/v1/status` and `/v1/recover` handlers, collect the Authorization header, and quiesce actuators before restarting. See `selftest/selftest.ino` for the no-actuator example. The shared page does not install an actuator handler on your behalf.

Open `http://DEVICE_ADDRESS/update`, enter the device password, and choose **Enter recovery**. After Wi-Fi reconnects, choose **Connect / keep recovery open**. This authenticated session persists Hold for a healthy image, so another power interruption does not silently launch it. **Launch verified firmware** explicitly exits recovery. Authentication remains required even during emergency recovery.

To install through that page, serve exactly one image from your computer:

```
python client.py serve --image build/huzzah32/selftest/binary/selftest.ino.bin --manifest build/huzzah32/selftest/binary/selftest.ino.manifest.json --bind YOUR_LAN_IP
```

Select that manifest in the page, paste the source URL printed by the command, and choose **Install / resume image**. Refresh status until phase 2 (Ready), then launch. Keep the source process running and permit its port on your trusted LAN. The device independently verifies the manifest digest against all downloaded bytes. A different unfinished image requires explicit authenticated `client.py ... abandon` before replacement. The CLI also supports `recover` and `maintenance` using the private `--key-file`.

Emergency use when the application is unreachable: disconnect power, reconnect, wait about two seconds, disconnect again before ten seconds elapse, then reconnect and leave power stable. Open the update page during the 15-minute window. A single ordinary power cycle resumes a previously healthy application after the ten-second startup window. Reset/EN buttons may produce the same cold-reset reason on qualified boards; verify behavior for each board profile.

Status fields `boot_window_mode` (0 = indefinite recovery, 1 = startup gesture, 2 = emergency maintenance) and `boot_window_ms` expose remaining time. In mode 2 the full 900000 ms remains pending while Wi-Fi is disconnected. Authentication does not disable watchdog or image verification.

`qualify_boot.py --key-file PRIVATE_JSON --port COM_PORT --output NEW_RECEIPT_DIRECTORY --bench-no-actuators` checks the gesture with CP2104 RTS/EN resets, the real 15-minute countdown value, authentication, a Hold surviving reset, normal startup and software entry. It does not substitute for physically removing all power or a full 15-minute hardware soak. Run only after restoring the good no-actuator selftest.
