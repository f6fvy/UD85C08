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
//      * "Send*" commands (open, close, toggle, ...). Synchronous: each
//        one waits for the board's answer (about 20 ms on real
//        hardware) and returns ResultCode::Timeout if none comes within
//        250 ms. A silent port is therefore reported instead of being
//        mistaken for a success. The call blocks for that long at most.
//      * "Query*" commands: synchronous, blocking calls that read a
//        relay or an input state, with a caller-chosen timeout.
//  - An optional event callback receives the state changes. The board
//    never sends anything by itself, so changes are discovered by
//    polling (StartPolling), and are also published at once for the
//    channel a Send* just acted on. The callback may be invoked from the
//    polling thread or from the thread that called Send* - a GUI consumer
//    (e.g. MFC) MUST NOT touch UI elements directly from it; forward the
//    event to the UI thread first (e.g. via PostMessage).
//
// What the board really does (answer formats, timing, relays changing
// state on their own...) is documented in DEVICE_BEHAVIOR.md.

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

        // --- State-changing commands ------------------------------------------
        // Each one waits for the board's answer (see the notes at the top):
        // Ok = the board confirmed, Timeout = nothing answered. On success a
        // RelayStateChanged event is raised for every relay whose known state
        // changed (SendLatch also reports the other relays as closed, which
        // is what the board does).
        OperationResult SendOpen(Channel channel);
        OperationResult SendClose(Channel channel);
        OperationResult SendToggle(Channel channel);
        OperationResult SendLatch(Channel channel);
        OperationResult SendMomentary(Channel channel);
        OperationResult SendAllOpen();
        OperationResult SendAllClose();
        OperationResult SendSetInputOutputRelation(InputOutputRelation relation);

        // Tells the library which input relation the board is in WITHOUT
        // sending anything (a board configured earlier, by another program or
        // in a previous session). A successful SendSetInputOutputRelation does
        // the same by itself. The board cannot be asked what mode it is in, so
        // after Connect the relation is unknown until one of the two is called.
        // It decides how StartPolling treats the relays:
        //  - Unrelated / SelfLocking / Interlocking (G0, G1, G2): the board
        //    has no timer involved, reading relays is harmless, and relay
        //    changes are reported at once.
        //  - Momentary (G3): the board pulses a relay by itself when its input
        //    changes, and reading a relay during that pulse cancels it. The
        //    polling therefore NEVER reads relays; their state comes from the
        //    commands sent. A pulse started by an input is not reported (the
        //    input change is).
        //  - unknown: best effort - a relay is left unread for 2 s after its
        //    input changed. A short tap can still go unseen and cancel a G3
        //    pulse, so a consumer that can know the relation should tell it.
        void SetKnownInputOutputRelation(InputOutputRelation relation);

        // AT protocol only (SupportsDelay() == false for HEX mode).
        // seconds must be in [0, 9999].
        OperationResult SendDelay(Channel channel, int seconds);

        bool SupportsDelay() const noexcept;

        // --- Queries (blocking, wait for the device's response) -------------
        // WARNING: on this board, reading a relay's state while that relay's
        // Momentary/Delay pulse is running CANCELS the pulse (it then stays
        // on). QueryRelayStatus talks to the board unconditionally, so do not
        // call it on a relay you just pulsed until the pulse is over (about
        // 0.5 s for Momentary, the delay for Delay). StartPolling already
        // takes care of this by itself. Input reads and reads of other relays
        // are harmless. See DEVICE_BEHAVIOR.md.
        OperationResult QueryRelayStatus(Channel channel, RelayState& outState, int timeoutMs = 500);
        OperationResult QueryInputStatus(Channel channel, InputState& outState, int timeoutMs = 500);

        // --- Event-driven monitoring ---------------------------------------------
        // Registers the callback invoked for asynchronous events
        // (RelayStateChanged, InputStateChanged, TransportError). Replaces
        // any previously registered callback. Pass nullptr to unregister.
        void SetEventCallback(EventCallback callback);

        // Starts a background thread that reads the 8 relays and the 8
        // inputs in a loop (pausing pollIntervalMs between rounds) and
        // raises RelayStateChanged / InputStateChanged through the callback
        // whenever a state differs from the last known one. The first round
        // reports every channel once, which lets a display initialise
        // itself. Calling it again restarts the thread (and that first
        // round).
        //
        // Polling is required: the board never sends anything unprompted,
        // and relays do change on their own (Momentary/Delay fall back,
        // Latch closes the others, input-driven relations G1..G3).
        //
        // After SendMomentary / SendDelay, the polling leaves that one relay
        // unread until its pulse is over (plus a safety margin), because
        // reading it would cancel the pulse. Everything else keeps being
        // polled, and the relay's LED/state then updates to "closed" a little
        // after the pulse ends (the relay is left alone for about 2 s after a
        // Momentary command, the requested time + 1 s after a Delay).
        //
        // The board can also act on a relay by itself when an input changes
        // (input relations G1..G3). What the polling does about it depends on
        // the relation (see SetKnownInputOutputRelation): in G3 relays are not
        // read at all; in G0..G2 nothing special is needed; when the relation
        // is unknown, each round reads an input BEFORE its relay and a relay is
        // left unread for 2 s after its input changed (a G3 pulse lasts 0.9 to
        // 1.3 s), which reports relay changes about two seconds late and can
        // still be fooled by a tap shorter than one polling round.
        //
        // The thread stops by itself, after raising ONE
        // EventType::TransportError, when:
        //  - the link fails (a non-timeout error): the USB cable was
        //    unplugged, or the board lost power - on this board, cutting the
        //    supply makes the COM port disappear from Windows; or
        //  - a whole round gets no answer at all (errorCode == Timeout): the
        //    port is open but nothing answers, i.e. it is not the board's
        //    port, or the board's firmware is not responding.
        // The consumer should then react (typically Disconnect(), refresh
        // its list of COM ports, and reconnect once the board is back);
        // StopPolling() remains safe to call afterwards.
        void StartPolling(int pollIntervalMs = 100);
        void StopPolling();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace UD85C08
