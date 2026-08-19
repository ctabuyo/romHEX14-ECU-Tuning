# Third-Party Notices

romHEX14 is distributed under GPL-3.0-or-later. It bundles or builds against the
following third-party components under their respective licenses:

| Component | Version | License | Location | Upstream |
|---|---|---|---|---|
| Lua | 5.4.8 | MIT | `third_party/lua-5.4/` | https://www.lua.org/ |
| sol2 | 3.5.0 | MIT | `third_party/sol2/` | https://github.com/ThePhD/sol2 |
| BLAKE3 | portable C reference | CC0-1.0 OR Apache-2.0 | `third_party/blake3/` | https://github.com/BLAKE3-team/BLAKE3 |
| Qt Advanced Docking System | 5.0.0 | LGPL-2.1-or-later | `third_party/qt-advanced-docking-system/` | https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System |

All component licenses are compatible with GPL-3.0-or-later. Full license texts
live alongside each vendored copy in `third_party/` (with Lua's `luaconf.h`
line endings normalized for the MinGW build).

For the Qt 6 framework linked at build time, see Qt's license terms shipped
with the Qt SDK (LGPL-3.0 or commercial).
