# Lua 5.4.7

The scripting language of mods (docs/mod-system.md, Scripting), run by
`port/src/gevr_modscript.c`.

- From https://www.lua.org/ftp/lua-5.4.7.tar.gz, SHA-256
  `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`
  (as lua.org publishes it). The files here are its `src/` unchanged.
- MIT licence: the notice at the end of `lua.h` (Copyright (C) 1994-2024
  Lua.org, PUC-Rio).
- Only what a sandboxed mod runs is here and built (CMakeLists.txt
  `SRC_LUA`): the core, the auxiliary library, and the base, coroutine,
  math, string, table and utf8 libraries. Left out on purpose: `liolib.c`,
  `loslib.c`, `loadlib.c` (`io`, `os`, `package`: files, processes and
  native libraries), `ldblib.c` (`debug`, which reaches past the sandbox),
  `linit.c` (which would open them all), and the stand-alone `lua.c` and
  `luac.c`. The interpreter only: no JIT exists for Lua 5.4, and iOS
  forbids one.
- To update: replace these files with a newer 5.4 release's, the same ones.
