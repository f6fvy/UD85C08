# third_party/naughter/

This folder must contain `SerialPort.h` from the **CSerialPort / CSerialPort2**
library by PJ Naughter (naughter.com), used here for low-level Win32 serial
port access (see `Core/src/SerialTransport.h/.cpp`).

## Why this file is not committed to this repository

Per the author's license, source code redistribution is only allowed for
the exact, unmodified versions released by the author himself, in order to
keep a single distribution point for the source code. To stay clearly
compliant (this repository may become public), `SerialPort.h` is excluded
from version control (see `.gitignore`) rather than redistributed here.

## How to get it

1. Download the zip from: <http://www.naughter.com/serialport.html>
   (direct link to the archive: <http://www.naughter.com/download/serialport.zip>)
2. Extract `SerialPort.h` from the archive.
3. Place it in this folder: `Core/third_party/naughter/SerialPort.h`

Version used during development of this project: **v1.43 (17 February
2022)**. Newer versions should work as-is (the class is header-only and
its public API has been stable across recent releases), but re-run the
self-tests (`UD85C08_CLI.exe --selftest`) after updating, just in case.
