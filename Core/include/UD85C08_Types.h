// UD85C08_Types.h
// Public API - shared types and data structures.
// Used by UD85C08_Controller.h and by any external consumer of the library.

#pragma once

#include <cstdint>
#include <string>

namespace UD85C08
{
    // ------------------------------------------------------------------
    // Hardware constants
    // ------------------------------------------------------------------
    constexpr int ChannelCount = 8;   // 8 relay outputs / 8 digital inputs

    // ------------------------------------------------------------------
    // Channel identifier (1 to 8, matching the protocol numbering)
    // ------------------------------------------------------------------
    using Channel = int;

    // ------------------------------------------------------------------
    // Relay operating modes (see AT / HEX protocol documentation)
    // ------------------------------------------------------------------
    enum class RelayMode
    {
        Toggle,       // AT+T / HEX command 0x03
        Latch,        // AT+L / HEX command 0x04 (interlock)
        Momentary,    // AT+M / HEX command 0x05
    };

    // ------------------------------------------------------------------
    // Binary state of a relay or an input
    // ------------------------------------------------------------------
    enum class RelayState
    {
        Unknown,
        Open,   // ON  - COM connected to NO
        Closed, // OFF - COM connected to NC
    };

    enum class InputState
    {
        Unknown,
        On,
        Off,
    };

    // ------------------------------------------------------------------
    // Global Input / Output relationship (AT+G0..G3 command)
    // ------------------------------------------------------------------
    enum class InputOutputRelation
    {
        Unrelated       = 0, // G0
        SelfLocking     = 1, // G1 (default)
        Interlocking    = 2, // G2
        Momentary       = 3, // G3
    };

    // ------------------------------------------------------------------
    // Protocol used to send commands
    // ------------------------------------------------------------------
    enum class ProtocolMode
    {
        At,   // ASCII, supports Delay
        Hex,  // 8-byte frame, no Delay but has a checksum
    };

    // ------------------------------------------------------------------
    // Operation result
    // ------------------------------------------------------------------
    enum class ResultCode
    {
        Ok,
        Timeout,
        NotConnected,
        InvalidChannel,
        InvalidChecksum,
        TransportError,
        UnexpectedResponse,
    };

    struct OperationResult
    {
        ResultCode code = ResultCode::Ok;

        bool IsOk() const noexcept { return code == ResultCode::Ok; }

        static OperationResult Success() noexcept { return OperationResult{ ResultCode::Ok }; }
        static OperationResult Error(ResultCode c) noexcept { return OperationResult{ c }; }
    };

    // Human-readable name for a ResultCode, e.g. for error messages/logs.
    // Returns an ASCII string literal; safe to embed in either a narrow
    // (CLI) or wide (MFC CString via "%hs") message.
    inline const char* ToString(ResultCode code) noexcept
    {
        switch (code)
        {
            case ResultCode::Ok:                 return "Ok";
            case ResultCode::Timeout:            return "Timeout";
            case ResultCode::NotConnected:       return "NotConnected";
            case ResultCode::InvalidChannel:     return "InvalidChannel";
            case ResultCode::InvalidChecksum:    return "InvalidChecksum";
            case ResultCode::TransportError:     return "TransportError";
            case ResultCode::UnexpectedResponse: return "UnexpectedResponse";
        }
        return "Unknown";
    }

    // ------------------------------------------------------------------
    // Asynchronous events (used by the event-driven / callback API,
    // primarily intended for GUI consumers that must not block their
    // main thread while waiting on the serial link).
    // ------------------------------------------------------------------
    enum class EventType
    {
        RelayStateChanged,   // a relay output changed state (reported by the device)
        InputStateChanged,   // a digital input changed state (from polling or push)
        TransportError,      // serial communication error (disconnect, timeout, ...)
    };

    struct Event
    {
        EventType  type = EventType::TransportError;
        Channel    channel = 0;       // meaningful for RelayStateChanged / InputStateChanged
        RelayState relayState = RelayState::Unknown;
        InputState inputState = InputState::Unknown;
        ResultCode errorCode = ResultCode::Ok; // meaningful for TransportError
    };

} // namespace UD85C08
