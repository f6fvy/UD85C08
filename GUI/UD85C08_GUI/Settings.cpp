// Settings.cpp

#include "pch.h"
#include "Settings.h"
#include <shlobj.h> // SHGetKnownFolderPath, FOLDERID_RoamingAppData
#include <iterator> // std::size

#pragma comment(lib, "Shell32.lib")

namespace
{
    // Builds "%APPDATA%\UD85C08\settings.ini", creating the "UD85C08"
    // folder if it does not exist yet. Returns an empty CString if
    // %APPDATA% could not be resolved.
    CString GetSettingsFilePath()
    {
        PWSTR appDataPath = nullptr;
        CString result;

        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath)))
        {
            CString folder;
            folder.Format(_T("%s\\UD85C08"), appDataPath);
            ::CoTaskMemFree(appDataPath);

            // Ignore the result: ERROR_ALREADY_EXISTS is fine, and any
            // other failure will simply surface later as a failed
            // Get/WritePrivateProfileString call.
            ::CreateDirectory(folder, nullptr);

            result = folder + _T("\\settings.ini");
        }

        return result;
    }

    // --- Section / key names, centralized here to avoid typos --------------
    const TCHAR* SectionConnection = _T("Connection");
    const TCHAR* KeyPort           = _T("Port");
    const TCHAR* KeyProtocol       = _T("Protocol");

    const TCHAR* SectionRelay      = _T("Relay");
    const TCHAR* KeyMode           = _T("Mode");
    const TCHAR* KeyDelay          = _T("Delay");

    const TCHAR* SectionIO         = _T("IO");
    const TCHAR* KeyRelation       = _T("Relation");

    // --- Protocol <-> string -------------------------------------------------
    CString ProtocolToString(UD85C08::ProtocolMode mode)
    {
        return (mode == UD85C08::ProtocolMode::At) ? _T("Text") : _T("Binary");
    }

    UD85C08::ProtocolMode ProtocolFromString(const CString& s)
    {
        return (s == _T("Binary")) ? UD85C08::ProtocolMode::Hex : UD85C08::ProtocolMode::At;
    }

    // --- Mode index <-> string -------------------------------------------------
    // Keep this list in sync with RelayModeIndex in UD85C08_GUIDlg.cpp.
    const TCHAR* const ModeNames[] = { _T("Normal"), _T("Momentary"), _T("Toggle"), _T("Latch"), _T("Delay") };
    constexpr int ModeCount = static_cast<int>(std::size(ModeNames));

    CString ModeIndexToString(int index)
    {
        if (index >= 0 && index < ModeCount)
            return ModeNames[index];
        return ModeNames[0]; // "Normal"
    }

    int ModeIndexFromString(const CString& s)
    {
        for (int i = 0; i < ModeCount; ++i)
        {
            if (s == ModeNames[i])
                return i;
        }
        return 0; // default: Normal
    }

    // --- I/O relation <-> string -------------------------------------------------
    CString IoRelationToString(UD85C08::InputOutputRelation relation)
    {
        switch (relation)
        {
            case UD85C08::InputOutputRelation::Unrelated:    return _T("Independent");
            case UD85C08::InputOutputRelation::SelfLocking:  return _T("Self-locking");
            case UD85C08::InputOutputRelation::Interlocking: return _T("Interlocking");
            case UD85C08::InputOutputRelation::Momentary:    return _T("Momentary");
        }
        return _T("Independent");
    }

    UD85C08::InputOutputRelation IoRelationFromString(const CString& s)
    {
        if (s == _T("Self-locking"))  return UD85C08::InputOutputRelation::SelfLocking;
        if (s == _T("Interlocking"))  return UD85C08::InputOutputRelation::Interlocking;
        if (s == _T("Momentary"))     return UD85C08::InputOutputRelation::Momentary;
        return UD85C08::InputOutputRelation::Unrelated;
    }
}

AppSettings LoadSettings()
{
    AppSettings settings; // start from the defaults declared in Settings.h

    CString path = GetSettingsFilePath();
    if (path.IsEmpty())
        return settings; // could not resolve %APPDATA% - just use defaults

    TCHAR buffer[256];

    ::GetPrivateProfileString(SectionConnection, KeyPort, _T(""), buffer, static_cast<DWORD>(std::size(buffer)), path);
    settings.port = buffer;

    ::GetPrivateProfileString(SectionConnection, KeyProtocol, _T("Text"), buffer, static_cast<DWORD>(std::size(buffer)), path);
    settings.protocol = ProtocolFromString(buffer);

    ::GetPrivateProfileString(SectionRelay, KeyMode, _T("Normal"), buffer, static_cast<DWORD>(std::size(buffer)), path);
    settings.modeIndex = ModeIndexFromString(buffer);

    settings.delaySeconds = ::GetPrivateProfileInt(SectionRelay, KeyDelay, 10, path);
    if (settings.delaySeconds < 0 || settings.delaySeconds > 9999)
        settings.delaySeconds = 10;

    ::GetPrivateProfileString(SectionIO, KeyRelation, _T("Independent"), buffer, static_cast<DWORD>(std::size(buffer)), path);
    settings.ioRelation = IoRelationFromString(buffer);

    return settings;
}

void SaveSettings(const AppSettings& settings)
{
    CString path = GetSettingsFilePath();
    if (path.IsEmpty())
        return; // could not resolve %APPDATA% - silently skip saving

    ::WritePrivateProfileString(SectionConnection, KeyPort, settings.port, path);
    ::WritePrivateProfileString(SectionConnection, KeyProtocol, ProtocolToString(settings.protocol), path);

    ::WritePrivateProfileString(SectionRelay, KeyMode, ModeIndexToString(settings.modeIndex), path);

    CString delayStr;
    delayStr.Format(_T("%d"), settings.delaySeconds);
    ::WritePrivateProfileString(SectionRelay, KeyDelay, delayStr, path);

    ::WritePrivateProfileString(SectionIO, KeyRelation, IoRelationToString(settings.ioRelation), path);
}
