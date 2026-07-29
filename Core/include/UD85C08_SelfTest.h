// UD85C08_SelfTest.h
// Public API - lightweight, dependency-free self-test.
//
// Exercises the AT and HEX codecs against the worked examples from the
// datasheet, plus a couple of Controller/MockSerialTransport round-trips.
// Does NOT require a physical UD85C08 board or serial port.
//
// Intended as a quick sanity check while developing without hardware
// available - not a replacement for a real unit test framework, but
// enough to catch regressions in the encoding/decoding logic.

#pragma once

namespace UD85C08
{
    // Runs all self-tests, printing a PASS/FAIL line per test to stdout.
    // Returns true if every test passed.
    bool RunSelfTests();

} // namespace UD85C08
