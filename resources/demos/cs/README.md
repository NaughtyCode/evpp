# CS Demo

This directory contains the Lua-only client/server demo as an independent
overlay under the resource tree. The base runtime scripts stay under
`resources/script/`; demo entry scripts, protocol code, and demo-specific config
live here.

Source layout:

- `script/client/init.lua` - client entry script
- `script/server/init.lua` - server entry script
- `script/shared/cs_protocol.lua` - newline-framed JSON protocol
- `config/client/client.json` - client config overlay
- `config/server/server.json` - server config overlay

`scripts/build_release.py` packages this overlay by default. In the generated
release folder the Lua demo is placed under `resources/demos/cs/script`, and the
demo config overlay points client/server script loading at that location.
