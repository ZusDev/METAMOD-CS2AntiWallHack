# Build dependencies and references

- AlliedModders' public-domain Source 2 sample build scripts were adapted for this
  port. Metamod SDK revision: `9c49d4c9733d94605901fb80148ea8c553f059d3`.
  https://github.com/alliedmodders/metamod-source
- AlliedModders CS2 HL2SDK revision: `3ea5305b0efe980fdf32ab4c87782e3343e33a90`.
  Its interface/header/library licenses remain in the fetched SDK.
  https://github.com/alliedmodders/hl2sdk/tree/cs2
- Extended transmit layout verified against Wend4r/sourcesdk and Source2ZE/CS2Fixes
  (GPL-3.0), revision `bb3be3449c122bebf2d74e85b61987de408ac339`.
  https://github.com/Source2ZE/CS2Fixes/blob/main/src/cs2_sdk/cchecktransmitinfo.h
- Trace signatures and entity-system offset verified against SwiftlyS2 core gamedata,
  revision `78b4c89a6e21de7b6a4d9485295b6448f58e26d9`. Swiftly is not a runtime dependency.
  https://github.com/swiftly-solution/swiftlys2
- nlohmann/json 3.11.3, MIT license, downloaded by setup.py. License is in the header.
  https://github.com/nlohmann/json/tree/v3.11.3
- Optional workspace verification uses Zig 0.13.0; not distributed with the plugin.

AntiWallHack port source is AGPL-3.0-only; dependencies retain their own licenses.
