# dist/ - published build of the UD85C08 library

This folder is **generated**, not edited: it is the only thing another Visual
Studio solution (for example the planned third project) needs in order to use
the library, without referencing the `UD85C08` project or its sources. Only
this README is tracked by Git; everything else is rebuilt (see "After a fresh
clone").

```
dist/
|-- README.md                         (this file)
|-- include/                          public headers (copy of Core/include/*.h)
`-- lib/<Platform>/<Configuration>/
        UD85C08.lib                   e.g. lib/x64/Debug/UD85C08.lib
                                           lib/x64/Release/UD85C08.lib
```

## How it is produced

By the **post-build event** of the `UD85C08` (Core) project: every time that
project is built, the public headers and the freshly linked `UD85C08.lib` are
copied here. `UD85C08_CLI` and `UD85C08_GUI` do not use `dist/`: inside the
`UD85C08` solution they reference the project directly.

## Using the library from another solution

In the consumer project's properties (set "All Configurations", and the right
platform):

| Property | Value |
|---|---|
| C/C++ > General > Additional Include Directories | `<path>\UD85C08\dist\include` |
| Linker > General > Additional Library Directories | `<path>\UD85C08\dist\lib\$(Platform)\$(Configuration)` |
| Linker > Input > Additional Dependencies | `UD85C08.lib` |

`$(Platform)\$(Configuration)` makes a Debug build pick the Debug library and
a Release build the Release one. In the code:

```cpp
#include "UD85C08_Controller.h"   // the only header normally needed
```

The consumer must match how the library was built, otherwise the link fails or
the program misbehaves:

- **Platform**: x64 (the one published so far).
- **Configuration**: Debug with Debug, Release with Release.
- **C++ Language Standard**: `/std:c++17`.
- **MFC**: "Use MFC in a shared DLL", and therefore the dynamic runtime
  (`/MD` in Release, `/MDd` in Debug). The library is built with MFC/ATL
  support because it uses ATL headers (through CSerialPort2); the public
  headers themselves only need the standard library.
- The Visual Studio "MFC and ATL" component must be installed.

## Things to know

- **After a fresh clone**, `dist/` does not exist: build the `UD85C08` project
  once in each configuration you need (at least x64 Debug and x64 Release).
- **The post-build step only runs when the library is actually relinked.** If
  `dist/` was deleted or looks stale, use Build > Rebuild on `UD85C08`.
- **Headers are never deleted from `dist/include`.** If a public header is
  renamed or removed in `Core/include`, delete `dist/include` and rebuild.
- **The library has no PDB**: debugging into the library's own code needs the
  `UD85C08` project, not just `dist/`.
