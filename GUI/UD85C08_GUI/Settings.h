// Settings.h
// Loads/saves the GUI's persistent settings to a plain .ini file under
// %APPDATA%\UD85C08\settings.ini (not the Windows Registry - see project
// discussion for why: a real, inspectable/editable file was requested).
//
// Deliberately kept separate from UD85C08_GUIDlg so the dialog code does
// not need to know about the .ini format, section/key names, or the
// string<->enum mappings used for storage.

#pragma once

#include "UD85C08_Types.h" // UD85C08::ProtocolMode, UD85C08::InputOutputRelation

struct AppSettings
{
    CString port;                  // e.g. "COM5"; empty if never saved/found
    UD85C08::ProtocolMode protocol = UD85C08::ProtocolMode::At;
    int modeIndex = 0;             // matches RelayModeIndex::ModeNormal in UD85C08_GUIDlg.cpp
    int delaySeconds = 10;
    UD85C08::InputOutputRelation ioRelation = UD85C08::InputOutputRelation::Unrelated;
};

// Reads settings from the .ini file. Missing file or missing/invalid
// keys fall back to sensible defaults (see AppSettings' default member
// initializers) - this never fails outright, so it is always safe to call.
AppSettings LoadSettings();

// Writes settings to the .ini file, creating the containing folder if
// needed. Silently does nothing if %APPDATA% cannot be resolved (should
// not happen in practice on a normal Windows user account).
void SaveSettings(const AppSettings& settings);
