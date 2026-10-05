---
name: gamma-firmware-restore
description: >-
  Restore official factory firmware or unbrick the this.is.NOISE Gamma Mini Synth
  (Electro-Smith Daisy Seed 2 DFM) via USB DFU when the device is unresponsive,
  hanging on the splash screen, or failing to connect to the official web updater.
---

# Gamma Firmware Restore Runbook

This skill provides the recovery workflow to unbrick and restore official factory firmware onto the **this.is.NOISE Gamma Mini Synth** (powered by the **Electro-Smith Daisy Seed 2 DFM**).

---

## Technical Background

1. **Bootloader Architecture:**
   - The device runs the **Electro-Smith Daisy Bootloader** residing in internal flash (`0x08000000`).
   - The user application/firmware is stored in external QSPI flash starting at **`0x90040000`** (`APP_TYPE = BOOT_QSPI` or `BOOT_SRAM`).
   - On power-up or hardware reset, the Daisy Bootloader enters a **~2.5-second DFU grace period** (USER LED pulses).
   - If no DFU connection occurs within ~2.5 seconds, it attempts to boot the application at `0x90040000`.
   - If the application at `0x90040000` is invalid or crashes, the bootloader emits an error/SOS blink pattern, stops DFU, and halts without enumerating USB CDC/MIDI.

2. **OLED Screen Retention:**
   - The 1.3" OLED (SSD1306/SH1106) maintains its display RAM (GDDRAM) as long as 3.3V logic power is supplied.
   - If firmware crashes or halts, the splash text (`this.is.NOISE inc`) remains visible on the screen even though the microcontroller is not running.

3. **Official Firmware Binaries:**
   - Official firmware binaries are hosted on the [official web update tool](https://gammaupdatetool.netlify.app/):
     - Latest release: `https://gammaupdatetool.netlify.app/data/gamma-v2.0.3.bin`
     - Legacy release: `https://gammaupdatetool.netlify.app/data/gamma1_1.bin`
   - Local copies are cached in `backups/gamma-v2.0.3.bin`.

---

## Recovery Procedures

### Method 1: Automated CLI Restore (Recommended)

Run the automated polling script:

```bash
python3 .agents/skills/gamma-firmware-restore/scripts/restore_firmware.py
```

**Steps:**

1. The script automatically verifies or downloads `backups/gamma-v2.0.3.bin` and starts polling `dfu-util` every 100ms.
2. Trigger bootloader mode on the Gamma:
   - **Option A:** Press the **RESET** button on the Daisy Seed 2 DFM (or cycle the power switch).
   - **Option B:** Hold **BOOT**, press and release **RESET**, then release **BOOT**.
3. The script catches the DFU device during the ~2-second window and flashes `gamma-v2.0.3.bin` to `0x90040000:leave`.
4. The device reboots automatically and re-enumerates as `Gamma`.

---

### Method 2: Manual Terminal Flash (`dfu-util`)

If flashing manually:

1. Put the Daisy Seed into bootloader mode (Hold **BOOT**, tap **RESET**, release **BOOT**, or tap **BOOT** during the startup grace period to extend it indefinitely).
2. Verify the DFU interface is active:

   ```bash
   dfu-util -l
   ```

   *Expected output:* `Found DFU: [0483:df11] ... Product: "Daisy Bootloader" (Electrosmith)`
3. Flash the firmware binary:

   ```bash
   dfu-util -a 0 -s 0x90040000:leave -D backups/gamma-v2.0.3.bin
   ```

   > [!NOTE]
   > `dfu-util: Error during download get_status` / exit code 74 upon `:leave` is normal. The STM32 resets immediately upon receiving the leave command, causing the follow-up USB query to disconnect.

---

### Method 3: Browser Recovery (Official Web Tool)

If using [gammaupdatetool.netlify.app](https://gammaupdatetool.netlify.app/):

1. Standard "Connect" requires entering the on-screen menu (`System -> Info -> Enter Boot`), which is inaccessible when custom firmware is broken.
2. Expand **Troubleshooting** $\rightarrow$ **"Can't Enter Boot"**.
3. Click the **"Connect & Install"** button (opens the browser USB pairing modal).
4. Power cycle or tap **RESET** on the Gamma.
5. Within the ~2-second boot window, select **"Daisy Bootloader"** and click **Connect**.
6. The web tool will flash `gamma-v2.0.3.bin` automatically.

---

## Post-Restore Verification

Check that the device enumerates correctly on the host system:

```bash
ioreg -p IOUSB -l -w0 | grep -A 10 "Gamma"
```

*Expected values:*

- `kUSBProductString` = `"Gamma"`
- `kUSBVendorString` = `"Electrosmith"`
- `idVendor` = `1155` (`0x0483`)
- `idProduct` = `22336` (`0x5740`)
- OLED screen displays the active Gamma synthesizer UI.
