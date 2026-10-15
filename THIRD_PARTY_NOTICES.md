# Third-Party Notices

## Victoria II reverse-engineering research

The [Maxioten research summaries](mappings/research/maxioten-import.md) attribute
factual findings to
[maxioten/Victoria2-Reverse-Engineering](https://github.com/maxioten/Victoria2-Reverse-Engineering)
at commit `e299a0214d32274239402e8ee22039e1d02e101f`. That tree supplies no
license file. Summaries of semantic findings, selected address/field anchors
and conflict annotations are independently written. No upstream tools, prose, reconstructed
function bodies, or patch implementations are included. This attribution does
not grant rights to upstream implementation or game material.

## Scripting Lua 5.1.5 runtime

The optional scripting plugin fetches and compiles its private Lua 5.1.5 runtime
from <https://www.lua.org/ftp/lua-5.1.5.tar.gz>. It does not use Victoria II's
Lua state or Lua DLLs.

Copyright (C) 1994-2012 Lua.org, PUC-Rio.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## MinHook v1.3.4

The kernel fetches MinHook v1.3.4 from
<https://github.com/TsudaKageyu/minhook> at commit
`c3fcafdc10146beb5919319d0683e44e3c30d537` and links it privately for x86
detour installation. MinHook is licensed under the 2-clause BSD license; its
license is retained at [`third_party/minhook-LICENSE.txt`](third_party/minhook-LICENSE.txt).

## toml++ v3.3.0

The kernel and launcher bundle toml++ v3.3.0 at
`third_party/toml/include/toml.hpp`, which includes its MIT license text and
UTF-8 decoder attribution.

Copyright (c) Mark Gillard <mark.gillard@outlook.com.au>

UTF-8 decoder: Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
