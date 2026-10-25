// UD85C08_Version.h
// Public API - library version (Semantic Versioning: MAJOR.MINOR.PATCH).
//
//  - MAJOR: incompatible change of the public API
//  - MINOR: new feature, backward compatible
//  - PATCH: bug fix, backward compatible
//
// Keep in sync with the top entry of CHANGELOG.md and with the git tag.

#pragma once

namespace UD85C08
{
    constexpr int VersionMajor = 1;
    constexpr int VersionMinor = 0;
    constexpr int VersionPatch = 0;

    // "MAJOR.MINOR.PATCH"
    constexpr const char* VersionString = "1.0.0";
}
