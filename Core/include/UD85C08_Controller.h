// UD85C08_Controller.h
// Public API - main entry point of the library.
// This is the ONLY header a consumer project (CLI, GUI, or any future
// project) needs to include in order to drive a UD85C08 board.
//
// Design notes:
//  - Uses the pImpl idiom: no third-party type (CSerialPort2, ATL, ...)
//    ever leaks into this header, keeping consumers fully decoupled
//    from the transport implementation (see project notes).
//  - Two families of methods are exposed:
//      * "Send*" commands: fire a state-changing command (open, close,
//        toggle, ...). These are synchronous and return as soon as the
//        command has been written to the wire (see .cpp for the exact
//        behaviour regarding the device's acknowledgement).
//      * "Query*" commands: synchronous, blocking calls that send a
//        request and wait for the corresponding response from the
//        device (with a timeout).
//  - An optional event callback can be registered to receive
//    asynchronous notifications, currently used for input polling
//    (see StartInputPolling). This callback may be invoked from a
//    background thread - a GUI consumer (e.g. MFC) MUST NOT touch UI
//    elements directly from it; forward the event to the UI thread
//    first (e.g. via PostMessage), as discussed in the project notes.
//
// IMPORTANT: this class has not yet been validated against a physical
// UD85C08 board (no hardware was available while writing it). The
// codecs were verified against every worked example in the datasheet,
// but a few response behaviours are assumptions - see the comments in
// AtCommandCodec.cpp / HexFrameCodec.h for the specific points that
// still need confirming with real hardware.

#pragma once

#include "UD85C08_Types.h"
#include "UD85C08_ISerialTransport.h"
#include <memory>
#include <string>
#include <functional>

namespace UD85C08
{
    using EventCallback = std::function<void(const Event&)>;

    class Controller
    {
    public:
        explicit Controller(ProtocolMode mode);

        // Same as above, but uses the given transport instead of the real
        // CSerialPort2-backed one. Intended for tests: pass a
        // UD85C08_MockSerialTransport.h instance to exercise the
        // command/response logic without any physical serial port.
        Controller(ProtocolMode mode, std::unique_ptr<ISerialTransport> transport);

        ~Controller();

        // Not copyable (owns a system resource: the serial port).
        Controller(const Controller&) = delete;
        Controller& operator=(const Controller&) = delete;

        // --- Connection -----------------------------------------------------

        // portName example: "COM5" (or "\\\\.\\COM12" for ports above 9).
        // The link is always opened at 9600 baud, 8N1 (fixed by the device).
        OperationResult Connect(const std::wstring& portName);
        void Disconnect();
        bool IsConnected() const noexcept;

        // --- State-changing commands (fire) ---------------------------------
        OperationResult SendOpen(Channel channel);
        OperationResult SendClose(Channel channel);
        OperationResult SendToggle(Channel channel);
        OperationResult SendLatch(Channel channel);
        OperationResult SendMomentary(Channel channel);
        OperationResult SendAllOpen();
        OperationResult SendAllClose();
        OperationResult SendSetInputOutputRelation(InputOutputRelation relation);

        // AT protocol only (SupportsDelay() == false for HEX mode).
        // seconds must be in [0, 9999].
        OperationResult SendDelay(Channel channel, int seconds);

        bool SupportsDelay() const noexcept;

        // --- Queries (blocking, wait for the device's response) -------------
        OperationResult QueryRelayStatus(Channel channel, RelayState& outState, int timeoutMs = 500);
        OperationResult QueryInputStatus(Channel channel, InputState& outState, int timeoutMs = 500);

        // --- Event-driven input monitoring -----------------------------------
        // Registers the callback invoked for asynchronous events (input
        // changes detected by polling, transport errors). Replaces any
        // previously registered callback. Pass nullptr to unregister.
        void SetEventCallback(EventCallback callback);

        // Starts a background thread that polls all 8 inputs every
        // pollIntervalMs and raises EventType::InputStateChanged through
        // the registered callback whenever a change is detected versus
        // the last known state. Safe to call again to change the interval
        // (restarts the polling thread).
        void StartInputPolling(int pollIntervalMs = 200);
        void StopInputPolling();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace UD85C08
