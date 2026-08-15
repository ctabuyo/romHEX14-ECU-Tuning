# Quick Start

Open your first ROM, view a map and save your work in five minutes. This walkthrough assumes you have a ROM binary (a `.bin`, `.hex`, `.rom` or similar dump) and, ideally, a matching A2L description file. If you only have the binary, romHEX 14 can still auto-detect maps for you.

## Step 1 — Create a project

Everything in romHEX 14 lives inside a **project**, which stores your ROM, its maps and your edit history in a single `.rx14proj` file.

1. From the **Project** menu choose **New Project…** (`Ctrl+N`), or click **New** on the toolbar.
2. In the **New Project** dialog, click **Browse…** next to the *ROM File* field and pick your binary. The file filter accepts `*.bin`, `*.rom`, `*.hex`, `*.dat`, `*.ori`, `*.mod`, `*.full` and `*.mpc`.
3. romHEX 14 inspects the file and pre-fills what it can — the ECU type is auto-detected from the ROM bytes, and the software number is scanned from the first 64 KB. Review and complete the **Vehicle / ECU Information** fields (Brand, Model, Engine, ECU Type, and so on). All fields are optional and editable.
4. Click **Create Project**. The button stays disabled until a ROM file is selected.

The project opens as a Hex document in the main workspace, and — unless you turned it off — a background scan begins looking for maps in the ROM (watch the amber "Scanning ROM for maps…" indicator in the status bar). Hex and map documents can be tabbed, split, floated, and re-docked.

## Step 2 — Import an A2L + HEX

If you have an A2L file for this ECU, importing it gives you named, correctly-scaled maps instead of raw byte regions.

1. With your project open, choose **Project ▸ Import A2L…**.
2. Pick the `.a2l` file. There is no separate binary picker here — the A2L is applied to the ROM you already loaded in Step 1, and the base address is detected automatically from the A2L itself.
3. In the **Import A2L – Select Maps** dialog, tick the groups or individual characteristics you want, then click **Import Selected**.
4. Review the **A2L Import Results** summary, which reports how many maps landed inside the ROM versus out of bounds, plus a compatibility score. Click **OK**.

See [A2L Import](06-a2l-import.md) for the full workflow, including what to do when the A2L does not match your ROM.

## Step 3 — Open a map

The left panel (**Map Selection**) lists every map in the project.

1. Use the **Filter maps…** box or the **All / Values / Curves / Maps** chips to narrow the list.
2. Double-click a map to open it. It appears in the active project window, where you can switch between the **Text** (hex), **2d** (waveform) and **3d** views using the tabs or the **View** menu (**Hex Editor**, **Waveform**, **3D Map**).

## Step 4 — Edit a value

1. In the 2D view, drag across the cells you want to change to select a byte range.
2. Type an adjustment using the **Selection** menu — for example **Value +1** (`+`), **Change absolute…** (`=`) to set an exact value, or **Change relative…** (`%`) to add a delta or a percentage such as `+5%`.
3. Prefer a visual edit? In the 2D view, hold **Shift** and drag the trace up or down to draw values directly.
4. Made a mistake? Press `Ctrl+Z` to undo. Every operation is a single undo step.

The [Map Editor](10-map-editor.md) chapter covers interpolation, smoothing, flattening and the 3D simulation view.

## Step 5 — Save and export

- **Save the project** with `Ctrl+S` (**Project ▸ Save**). By default romHEX 14 also auto-saves 5 seconds after your last edit — the status bar shows "● Modified" or "✓ Saved".
- **Export the finished binary** with **Project ▸ Export ROM…** (`Ctrl+E`), which writes a `.bin` or `.rom` file ready to flash.

!!! tip "Correct the checksum first"
    Most ECUs reject a modified ROM unless its checksum is repaired. In Pro builds, run **Project ▸ Correct Checksum…** before exporting. See [Checksum Manager](11-checksum-manager.md).

## Where to next?

- [Projects](05-projects.md) — versions, Tuning Branches and multi-ROM projects
- [A2L Import](06-a2l-import.md) — the most powerful import workflow
- [Map Editor](10-map-editor.md) — 2D tables, 3D surfaces and value operations
- [Checksum Manager](11-checksum-manager.md) — verify and repair checksums before flashing
