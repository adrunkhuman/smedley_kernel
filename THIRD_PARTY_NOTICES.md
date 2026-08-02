# Third-Party Notices

## Victoria II-facing Lua 5.1.4 headers

The kernel uses checked-in Lua 5.1.4 headers matching Victoria II. They are
bundled at [`third_party/victoria2-lua/include`](third_party/victoria2-lua/include),
including the MIT license in
[`COPYRIGHT`](third_party/victoria2-lua/include/COPYRIGHT).

## Scripting Lua 5.1.5 runtime

The optional scripting plugin fetches and compiles its private Lua 5.1.5 runtime
from <https://www.lua.org/ftp/lua-5.1.5.tar.gz>. It does not use the Victoria
II-facing headers.

## toml++ v3.3.0

The kernel and launcher bundle toml++ v3.3.0 at
[`third_party/toml/include/toml.hpp`](third_party/toml/include/toml.hpp), which
includes its MIT license text.
