# Checksum Manager

ECUs store one or more checksums that guard the integrity of the flash. When you change a map, those checksums no longer match, and the ECU will usually refuse the file. The Checksum Manager verifies and repairs the checksum for your ECU so your modified ROM is accepted.

## Why checksums matter

A checksum is a value computed over a region of the ROM and stored back in the ROM. After editing, the stored value is stale. If you flash a ROM with a wrong checksum, most ECUs reject it (or run in a limp/default mode). Always verify, and correct if needed, before exporting a ROM for flashing.

## The Checksum Manager dialog

Both actions live on the **Project** menu and share a small **Select Checksum Algorithm** dialog:

1. Choose **Project ▸ Verify Checksum** or **Project ▸ Correct Checksum…**.
2. The dialog shows your project's **ECU type** and an **auto-detected algorithm** (romHEX 14 scans the ROM for embedded ECU identifiers such as "MED17", "EDC17", "EDC15P", "ME7." or "SIMOS" and validates that the algorithm's address ranges fit the ROM).
3. Confirm or change the selection in the **Select algorithm** dropdown, then click **OK**.

## Cross-Platform Plugins (Linux & macOS)

On Linux and macOS, romHEX 14 loads standalone open-source dynamic plugins (`IChecksumPlugin`) located in `plugins/checksums/`:

- **Bosch MED17 / EDC17**: Fully implemented native C++ engine with TriCore partition scanning, modular 1024-bit RSA signature calculation, and block checksum correction.
- **Work-in-Progress Plugins**: Additional ECU families (Bosch EDC15/ME7/EDC16/MED9, Siemens PCR2.1/SID/MS, Delphi DCM, Marelli MultiJet, Denso, etc.) provide architecture stubs that signal active development.

## Checksum DLL bridge (Windows)

On Windows, romHEX 14 uses a library of 32-bit ECU checksum modules (`DEV001.dll`, `DEV002.dll`, …) covering a wide range of Bosch, Siemens/Continental, Delphi, Denso, Marelli, Motorola and other ECUs across cars and trucks.

Because romHEX 14 itself is 64-bit, it runs these 32-bit modules through a helper process, **`checksumhelper.exe`**, which passes the ROM back and forth via temporary files. The helper is searched for in `ChecksumDLL/checksumhelper.exe`, next to the application executable, or one directory up.

!!! warning "Windows prerequisite: Visual C++ 2005 SP1 (x86) runtime"
    The 32-bit checksum modules link against the **Microsoft Visual C++ 2005 SP1 Redistributable (x86)** runtime. If it is not installed, the modules fail to load and romHEX 14 reports a missing dependency error.

## Verifying a ROM

Run **Project ▸ Verify Checksum**, pick the algorithm and click **OK**:

- **Match** — the status bar shows "Checksum OK — *ECU* (*algorithm*)".
- **Mismatch** — a dialog reports "✗ Checksum mismatch" and advises using **Correct Checksum** before flashing.
- **Unsupported** — if no algorithm matches the ECU, an information dialog explains that verification is not available for it.

## Recalculating before save

Run **Project ▸ Correct Checksum…** to repair the checksum before you export:

1. Choose the algorithm and click **OK**.
2. A confirmation dialog restates the ECU and algorithm and notes that the correction "modifies ROM data in memory (not saved until export)". Click **Yes**.
3. On success the status bar shows "Checksum corrected — *ECU* (*algorithm*)".

The correction is applied to the in-memory ROM and marks the project modified. It is written to disk only when you save the project or, for flashing, when you use **Export ROM…**. As a rule: edit your maps, **Correct Checksum**, then **Export ROM** — in that order.
