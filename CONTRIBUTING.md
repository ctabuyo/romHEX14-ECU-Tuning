# Contributing to romHEX 14

Thank you for your interest in contributing to romHEX 14! Whether you're fixing a bug, adding a feature, improving documentation, or reporting an issue — your contribution is valued.

## Getting Started

1. **Fork** the repository on [GitHub](https://github.com/ctabuyo/romHEX14-community) or [Gitee](https://gitee.com/ctabuyo/romHEX14-community)
2. **Clone** your fork locally
3. **Build** the project (see [README.md](README.md#building-from-source))
4. **Create a branch** for your work: `git checkout -b feature/my-feature`

## Development Setup

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6 -DCMAKE_BUILD_TYPE=Debug
cmake --build . --parallel
```

The community edition builds with `RX14_PRO_BUILD=OFF` (the default).

## Code Style

| Convention | Example |
|---|---|
| Indentation | 4 spaces (no tabs) |
| Methods | `camelCase()` |
| Classes | `PascalCase` |
| Members | `m_camelCase` |
| Local variables | `camelCase` |
| Constants | `kPascalCase` or `ALL_CAPS` |
| Files | `lowercase.cpp` / `lowercase.h` |

### General Guidelines

- Use C++17 features where appropriate (structured bindings, `if constexpr`, `std::optional`, etc.)
- Follow Qt6 conventions for signal/slot connections (`connect()` with function pointers)
- Prefer `QStringLiteral()` over `QString("")` for string literals
- Every new `.cpp`/`.h` file must include the copyright header (see any existing file for the template)

## Internationalization (i18n)

romHEX 14 supports 4 languages: English, Chinese (简体中文), Spanish (Español), and Thai (ไทย). **All user-visible strings must be translatable.**

### Rules

1. **Wrap every UI string in `tr()`** — menu items, labels, tooltips, error messages, status bar text
2. **Don't translate**: brand names, keyboard shortcuts, file extensions, format strings like `"0x%1"`, HTML/CSS markup
3. **Update `retranslateUi()`** — if you add a new `QAction` or `QDockWidget`, add a corresponding `setText(tr(...))` / `setWindowTitle(tr(...))` call in `MainWindow::retranslateUi()` so the text updates when the user switches language at runtime

### Updating Translation Files

After adding or changing any `tr()` strings, run `lupdate` to extract them into the `.ts` files:

```bash
# From the project root:
lupdate src/ -ts translations/rx14_en.ts translations/rx14_zh_CN.ts translations/rx14_es.ts translations/rx14_th.ts -no-obsolete
```

If you don't have `lupdate` in your PATH, it's in your Qt installation:
```bash
~/Qt/6.8.3/macos/bin/lupdate src/ -ts translations/rx14_*.ts -no-obsolete
```

This will:
- Add new `tr()` strings as `<translation type="unfinished">` entries
- Remove obsolete strings that no longer exist in the code

### Providing Translations

If you can translate to any of the supported languages:

1. Open the relevant `.ts` file in [Qt Linguist](https://doc.qt.io/qt-6/linguist-translators.html) or a text editor
2. Find entries marked `type="unfinished"` 
3. Fill in the `<translation>` text
4. Remove the `type="unfinished"` attribute
5. Include the updated `.ts` files in your PR

You don't need to compile `.qm` files — CMake does that automatically at build time.

### Quick Checklist for PRs

- [ ] All new UI strings wrapped in `tr()`
- [ ] New actions/docks added to `retranslateUi()`
- [ ] `lupdate` run and `.ts` files updated
- [ ] English `.ts` entries filled (copy source text)
- [ ] Other languages: provide translations if you can, otherwise leave as `unfinished`

## Submitting Changes

### Pull Requests

1. Keep PRs focused — one feature or fix per PR
2. Write a clear title and description explaining **what** and **why**
3. If your PR addresses an issue, reference it: `Fixes #123`
4. Make sure the project builds without errors before submitting
5. Run `lupdate` and include updated `.ts` files

### Commit Messages

Use clear, imperative commit messages:

```
Add map pack export dialog

Implement the export dialog that lets users select which maps to
include in the .rxpack file. Supports filtering by group and
drag-and-drop reordering.
```

## Reporting Issues

- **GitHub Issues** — preferred for English speakers
- **Gitee Issues** — available for Chinese users who cannot access GitHub

When reporting a bug, please include:
- romHEX 14 version (from Help → About)
- Operating system and version
- Steps to reproduce
- Expected vs actual behavior
- Screenshots if applicable

## Architecture Overview

romHEX uses a Qt6 document workspace powered by [Qt Advanced Docking System
(ADS)](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System), not
`QMdiArea`. ADS 5.0.0 is fetched by CMake at configure time and pinned to its
release commit in `CMakeLists.txt`.

- `MainWindow` — Top-level window, menu bar, toolbar, project tree, and the
  shared `ads::CDockManager` workspace host.
- `Project` — The project document: ROM bytes, maps, metadata, and transactional
  edit history.
- `ProjectView` — One Hex / waveform / 3D document dock per project.
- `MapEditorView` — A pure map-editor widget hosted in one document dock per
  project/map pair. It is not a dialog and must not own document lifecycle.
- `MapValueCodec` — The single conversion boundary for map-cell raw bytes,
  scaled physical values, numeric bounds, and floating-point safety.
- `src/io/ols/` — OLS/KP file format parser (import only in community edition)
- `translations/` — Qt Linguist `.ts` files for all 4 languages

When adding a document-style view, keep its content widget independent of its
ADS frame. `MainWindow` owns dock creation, activation, closing, and project
lifecycle; the content widget owns only its UI and emits requests for project
operations. Route ROM mutations through `Project::applyRomPatches()` so every
edit participates in undo/redo and live view updates.

## License

By contributing to romHEX 14, you agree that your contributions will be licensed under the [GNU General Public License v3.0](LICENSE).
