# User Interface

A guided tour of the panels, toolbars, menus and docks in the romHEX 14 main window.

## Main window layout

The window is a horizontal split with three regions:

- **Left — Map Selection panel:** the filterable list/tree of every map in the active project.
- **Centre — workspace:** an ADS document workspace. Each project has a Hex document with **Text** (hex), **2d** (waveform), and **3d** views; map tables open as independent map-editor documents. Documents can be tabbed, split, floated, and dragged back into the workspace.
- **Right — AI Assistant panel:** a chat panel, hidden until you open it.

Two dockable panels — **Differences** and **Tuning Branches** — can be shown on the side as needed. The **Window** menu manages document layouts and workspace perspectives.

## Menu bar

romHEX 14 has nine menus:

- **Project** — new/open/save projects, all imports (A2L, OLS, KP, Map Pack, CSV) and exports, version snapshots, linking ROMs, comparisons, checksums, patches and the Project Manager.
- **Edit** — move to the previous/next map (`Ctrl+Left` / `Ctrl+Right`) and **Find Map…** (`Ctrl+F`).
- **View** — switch the active view (**Hex Editor / Waveform / 3D Map**), toggle the **AI Assistant** (`Ctrl+\`), **Differences** (`Ctrl+D`), **Tuning Branches** (`Ctrl+B`) and **Differences vs Original** (`Ctrl+Shift+O`), and zoom the hex font in/out.
- **Selection** — the value operations (see [Map Editor](10-map-editor.md)) and the **Sync cursors** toggle.
- **Find** — jump to an address, find similar files, and insert/navigate comments and markers.
- **Miscellaneous** — **Project Info…**, auto-detect maps/ECU, the **Command Palette** (`Ctrl+K`), **Settings…**, the **Auto Save** modes and the **Language** submenu.
- **Datalog** — open and compare logs and run Lua scripts.
- **Window** — tile, cascade and **Compare Projects…**.
- **Help** — About, and (in Pro builds) update checks and your account.

## Toolbars

Two toolbars sit under the menu bar:

- **Project** toolbar — quick access to Home, New, Project Manager, Save/Save As, the imports (A2L, OLS, KP, CSV), Save Version Snapshot, Export ROM, Close, Tile/Cascade, previous/next map, **Sync cursors** and the **AI** panel toggle.
- **Format** toolbar — how the bytes are interpreted: **data size** (8 / 16 / 32 / F for float), **byte order** (LE / BE), **sign** (± / +), **display format** (decimal / hex / binary / percent), plus difference-to-original and ignore-map toggles, custom scaling, the height-colour toggle and a hex font-size spinner.

You can show or hide either toolbar from the toolbar right-click menu.

## Project tree

The left panel's tree lists every map in the active project. Its behaviour:

- A **Filter maps…** box narrows the list as you type.
- Filter chips select **All / Modified / Starred / Recent** and **Values / Curves / Maps**.
- Multi-select is enabled, so you can edit several maps at once.
- Extra nodes appear for imported structure — for example a **Versions** node for saved snapshots.
- Double-click a map to open it; right-click for per-map actions.

## Map list panel

The Map Selection panel's title bar carries the **✦ AI Translate** button (Pro; translates map names, when the translation module is active) and a "show only modified maps" toggle. This is the same left panel described above — "project tree" and "map list" refer to the one filterable list.

## Hex view dock

romHEX 14 does not use a separate floating hex dock. The raw byte view is the **Text** tab inside each project window, alongside the **2d** and **3d** tabs. See [Hex Overview](09-hex-overview.md).

## AI assistant dock

The **AI Assistant** panel docks on the right. Toggle it with **View ▸ AI Assistant** (`Ctrl+\`) or the **AI** toolbar button. It is a chat panel that has access to the active project and every open project. The AI *tuning functions* and *map translation* are Pro features, but the chat itself runs on an API key you provide. See [AI Functions](13-ai-functions.md).

## Status bar

The status bar shows:

- transient messages ("Project saved: …", "A2L imported: …", parse progress, and so on);
- a **scan indicator** ("Scanning ROM for maps…") while a background map detection runs;
- an **auto-save indicator** ("● Modified" / "✓ Saved *N*s ago");
- your **account** state (Pro builds).

## Customising the layout

- Drag the splitter handles to resize the left panel and AI panel.
- Show/hide the **Differences** and **Tuning Branches** docks from the **View** menu; they are movable and floatable.
- Show/hide toolbars from their right-click menu.
- Set the hex font size with the Format toolbar spinner or **View ▸ Zoom In / Zoom Out**.
- Choose a colour theme and adjust individual colours in **Settings** (see [Settings & Localization](15-settings.md)).
