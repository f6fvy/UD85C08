// PortName.h
// Internal to UD85C08_Core - normalization of serial port names.
//
// Windows only accepts a bare "COMn" name for n = 1..9. From COM10 upward
// the device must be opened as "\\.\COMn" (verified on a real board:
// "COM12" fails, "\\.\COM12" works). The prefix is also valid for COM1..9,
// so it is simply always added. Callers (CLI, GUI, or any other project)
// can therefore pass either form and never need to know about this quirk.
//
// Kept free of any Windows header so it can be unit-tested anywhere.

#pragma once

#include <string>

namespace UD85C08
{
    inline std::wstring NormalizePortName(const std::wstring& portName)
    {
        if (portName.empty())
            return portName; // nothing to normalize: opening will fail on its own

        static const std::wstring prefix = L"\\\\.\\";

        if (portName.compare(0, prefix.size(), prefix) == 0)
            return portName; // already in the extended form

        return prefix + portName;
    }

} // namespace UD85C08
