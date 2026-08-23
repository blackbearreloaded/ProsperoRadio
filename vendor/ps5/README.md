# PS5 dependency snapshot

This directory contains the small set of PS5 build artifacts needed by the
first RmlUi + SDL smoke target.

- SDL2 headers and `libSDL2.a` come from the local PS5 SDL build.
- RmlUi headers and `librmlui.a` come from the local RmlUi 6.2 build with
  `RMLUI_FONT_ENGINE=none`, Lua bindings disabled, and
  `-fno-exceptions` to remove unused exception metadata.
- The PS5 compiler emits `.ctors`; the checked-in SharpProspero compatibility
  patch handles constructor arrays and COMDAT section symbols.
- C++ runtime archives and libc/kernel stub catalogs come from the installed
  PS5 Payload SDK in WSL.

The current RmlUi archive is a link/lifecycle smoke dependency. It does not
render text because no font engine is enabled. Rebuild RmlUi with the PS5
FreeType package before treating the UI as feature-complete.
