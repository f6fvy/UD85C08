// UD85C08_Controller.cpp

#include "UD85C08_Controller.h"
#include "SerialTransport.h" // also brings in UD85C08_ISerialTransport.h
#include "AtCommandCodec.h"
#include "HexFrameCodec.h"

#include <array>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace UD85C08
{
    namespace
    {
        // The board answers within about 20 ms (measured), so these are
        // generous. CommandTimeoutMs is how long a Send* call can block the
        // caller (typically a GUI thread) when nothing answers.
        constexpr int CommandTimeoutMs   = 250;
        constexpr int PollQueryTimeoutMs = 100;

        // Reading a relay's state while its Momentary/Delay pulse is running
        // CANCELS the pulse (measured on the real board, see DEVICE_BEHAVIOR.md;
        // reads of the inputs or of other relays are harmless). So after such a
        // command the relay is left alone for the nominal pulse length plus a
        // safety margin. A Momentary command pulse lasts 0.8 to 1.0 s on this board
        // (measured, AT and HEX alike - not the 0.5 s of the datasheet).
        constexpr int MomentaryQuietMs   = 2000;
        constexpr int DelayQuietExtraMs  = 1000; // added to the requested delay

        // The input relations G1..G3 make the board act on a relay by itself when
        // its input changes (G3 starts a pulse), and the library cannot tell when.
        // So whenever an input is seen to change, the matching relay is left
        // unread for this long. The pulse started by an input in G3 was measured
        // on the real board at between 0.9 and 1.3 s (NOT the 0.5 s of the
        // datasheet), so 2 s leaves a margin of 0.7 s. The price: a relay switched
        // by its input (G1, G2) is reported about 2 s late.
        constexpr int InputEdgeQuietMs   = 2000;

        // Used to discard stale bytes right after opening the port.
        constexpr int DrainTimeoutMs = 20;
        constexpr int DrainMaxReads  = 10;

        int MillisecondsUntil(const std::chrono::steady_clock::time_point& deadline)
        {
            auto remaining = deadline - std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
            return ms > 0 ? static_cast<int>(ms) : 0;
        }
    }

    struct Controller::Impl
    {
        std::unique_ptr<IProtocolCodec> codec;
        std::unique_ptr<ISerialTransport> transport;

        // Serializes all access to transport+codec: application-thread
        // commands and the background polling thread all go through this
        // mutex, so a command and a poll never interleave on the wire.
        std::mutex ioMutex;

        EventCallback callback;
        std::mutex callbackMutex;

        std::thread pollingThread;
        std::atomic<bool> pollingActive{ false };
        int pollIntervalMs = 100;

        // Last known states (index 1..8 used), shared between the polling
        // thread and the threads calling Send*. Guarded by stateMutex.
        std::mutex stateMutex;
        std::array<RelayState, ChannelCount + 1> lastRelayState{};
        std::array<InputState, ChannelCount + 1> lastInputState{};

        // The input relation the board is known to be in: -1 = unknown, else the
        // numeric value of InputOutputRelation. Set by SendSetInputOutputRelation
        // / SetKnownInputOutputRelation, forgotten on Connect (the board cannot
        // be asked, and it remembers its mode across power cycles).
        std::atomic<int> knownRelation{ -1 };

        // Which channels the polling reads (bit 0 = channel 1).
        std::atomic<ChannelMask> pollRelayMask{ AllChannels };
        std::atomic<ChannelMask> pollInputMask{ AllChannels };

        // Until when a relay must not be read, because its own pulse is running
        // (index 1..8). Default-constructed = the epoch = "not quiet". Guarded
        // by stateMutex.
        std::array<std::chrono::steady_clock::time_point, ChannelCount + 1> quietUntil{};

        explicit Impl(ProtocolMode mode, std::unique_ptr<ISerialTransport> injectedTransport = nullptr)
        {
            if (mode == ProtocolMode::At)
                codec = std::make_unique<AtCommandCodec>();
            else
                codec = std::make_unique<HexFrameCodec>();

            transport = injectedTransport ? std::move(injectedTransport)
                                           : std::unique_ptr<ISerialTransport>(std::make_unique<SerialTransport>());

            ResetKnownStates();
        }

        void ResetKnownStates()
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            lastRelayState.fill(RelayState::Unknown);
            lastInputState.fill(InputState::Unknown);
            quietUntil.fill(std::chrono::steady_clock::time_point{});
        }

        void BeginQuietPeriod(Channel ch, int durationMs)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            quietUntil[ch] = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
        }

        // Like BeginQuietPeriod, but never SHORTENS a protection already in place
        // (e.g. a 60 s Delay must not be cut short by an input edge).
        void ExtendQuietPeriod(Channel ch, int durationMs)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(durationMs);
            if (until > quietUntil[ch])
                quietUntil[ch] = until;
        }

        void EndQuietPeriod(Channel ch)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            quietUntil[ch] = std::chrono::steady_clock::time_point{};
        }

        bool IsQuiet(Channel ch)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            return std::chrono::steady_clock::now() < quietUntil[ch];
        }

        void RaiseEvent(const Event& ev)
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            if (callback)
                callback(ev);
        }

        // Records a relay's state and raises RelayStateChanged if it differs
        // from the last known one.
        void PublishRelayState(Channel ch, RelayState state)
        {
            bool changed;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                changed = (lastRelayState[ch] != state);
                lastRelayState[ch] = state;
            }

            if (changed)
            {
                Event ev;
                ev.type = EventType::RelayStateChanged;
                ev.channel = ch;
                ev.relayState = state;
                RaiseEvent(ev);
            }
        }

        // Records an input's state and raises InputStateChanged if it differs from
        // the last known one. Returns true when an input that WAS known has just
        // changed (an edge) - the first observation of an input is not one.
        bool PublishInputState(Channel ch, InputState state)
        {
            bool changed;
            bool edge;
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                const InputState previous = lastInputState[ch];
                changed = (previous != state);
                edge = changed && previous != InputState::Unknown;
                lastInputState[ch] = state;
            }

            if (changed)
            {
                Event ev;
                ev.type = EventType::InputStateChanged;
                ev.channel = ch;
                ev.inputState = state;
                RaiseEvent(ev);
            }

            return edge;
        }

        // Writes a command and waits (up to timeoutMs) for a decoded
        // response matching expectedKind. If expectedChannel is non-zero,
        // only a response reporting that exact channel (or the special
        // "all channels" value 0) is accepted; unrelated/mismatched
        // messages seen in the meantime are ignored.
        OperationResult Query(const ByteBuffer& cmd,
                               ResponseKind expectedKind,
                               Channel expectedChannel,
                               DecodedResponse& outResponse,
                               int timeoutMs)
        {
            if (cmd.empty())
                return OperationResult::Error(ResultCode::InvalidChannel);

            std::lock_guard<std::mutex> lock(ioMutex);

            if (!transport->IsOpen())
                return OperationResult::Error(ResultCode::NotConnected);

            OperationResult writeResult = transport->Write(cmd);
            if (!writeResult.IsOk())
                return writeResult;

            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            ByteBuffer rxBuffer;

            while (std::chrono::steady_clock::now() < deadline)
            {
                ByteBuffer chunk;
                OperationResult readResult = transport->Read(chunk, 64, MillisecondsUntil(deadline));

                if (readResult.IsOk())
                {
                    rxBuffer.insert(rxBuffer.end(), chunk.begin(), chunk.end());
                }
                else if (readResult.code != ResultCode::Timeout)
                {
                    return readResult; // real transport error, propagate
                }

                // Try to decode as many complete messages as are already
                // buffered before reading more bytes.
                for (;;)
                {
                    DecodedResponse resp;
                    size_t consumed = 0;
                    bool decoded = codec->TryDecode(rxBuffer, resp, consumed);

                    if (consumed > 0)
                        rxBuffer.erase(rxBuffer.begin(), rxBuffer.begin() + consumed);

                    if (!decoded)
                    {
                        // consumed > 0 means the codec just skipped junk or padding
                        // to resynchronize: what is left in the buffer may already
                        // hold a complete message, so try again before reading more.
                        if (consumed > 0)
                            continue;
                        break; // genuinely incomplete: go read more bytes
                    }

                    if (resp.kind == expectedKind &&
                        (expectedChannel == 0 || resp.channel == expectedChannel || resp.channel == 0))
                    {
                        outResponse = resp;
                        return OperationResult::Success();
                    }
                    // Otherwise: unrelated/unrecognized message, discard and keep looking.
                }
            }

            return OperationResult::Error(ResultCode::Timeout);
        }

        // Sends a command that makes the board answer with a relay state
        // (open, close, toggle, latch, momentary, delay, all open/close).
        // On success, publishes the state the board reported. 'exclusive'
        // is for Latch: the board then closes every other relay silently.
        // 'quietMs' > 0 is for Momentary/Delay: the relay is then not read
        // for that long (see the constants above).
        OperationResult SendRelayCommand(const ByteBuffer& cmd, Channel channel, bool exclusive, int quietMs = 0)
        {
            // Start the quiet period BEFORE sending, so the polling thread cannot
            // slip a read in between the board starting the pulse and us noting it.
            const bool quiet = (quietMs > 0 && channel >= 1 && channel <= ChannelCount);
            if (quiet)
                BeginQuietPeriod(channel, quietMs);

            DecodedResponse reply;
            OperationResult result = Query(cmd, ResponseKind::RelayStatus, channel, reply, CommandTimeoutMs);
            if (!result.IsOk())
            {
                if (quiet)
                    EndQuietPeriod(channel); // the command most likely never reached the board
                return result;
            }

            if (reply.channel == 0)
            {
                // "All channels" answer (OpenA / CloseA).
                for (Channel ch = 1; ch <= ChannelCount; ++ch)
                {
                    PublishRelayState(ch, reply.relayState);
                    if (reply.relayState == RelayState::Closed)
                        EndQuietPeriod(ch); // a closed relay has no pulse left to protect
                }
            }
            else
            {
                if (exclusive && reply.relayState == RelayState::Open)
                {
                    for (Channel ch = 1; ch <= ChannelCount; ++ch)
                    {
                        if (ch != reply.channel)
                        {
                            PublishRelayState(ch, RelayState::Closed);
                            EndQuietPeriod(ch);
                        }
                    }
                }
                PublishRelayState(reply.channel, reply.relayState);
                if (reply.relayState == RelayState::Closed)
                    EndQuietPeriod(reply.channel);
            }

            return OperationResult::Success();
        }

        // Discards whatever is already waiting on the port, so that a stale
        // reply from an earlier session cannot be mistaken for a fresh one.
        // Must be called with ioMutex held.
        void DrainInput()
        {
            for (int i = 0; i < DrainMaxReads; ++i)
            {
                ByteBuffer chunk;
                if (!transport->Read(chunk, 256, DrainTimeoutMs).IsOk())
                    break;
            }
        }

        void PollLoop()
        {
            // A full round is 8 relay reads + 8 input reads. If that many
            // queries in a row get no answer at all, nothing is listening
            // (the count is in queries, so it also holds with masks).
            const int maxConsecutiveTimeouts = 2 * ChannelCount;
            int consecutiveTimeouts = 0;

            // Returns false when polling must end (an error event was raised).
            auto handle = [&](const OperationResult& result, Channel ch) -> bool
            {
                if (result.IsOk())
                {
                    consecutiveTimeouts = 0;
                    return true;
                }

                if (result.code == ResultCode::Timeout &&
                    ++consecutiveTimeouts < maxConsecutiveTimeouts)
                {
                    return true; // one slow answer is not an error
                }

                // Either the link failed (e.g. USB unplugged), or nothing answered
                // for a whole round. Report it ONCE and stop, instead of raising the
                // same error for every channel of every round forever. The consumer
                // decides what to do (typically: Disconnect()).
                Event ev;
                ev.type = EventType::TransportError;
                ev.channel = ch;
                ev.errorCode = result.code;
                RaiseEvent(ev);

                pollingActive.store(false);
                return false;
            };

            while (pollingActive.load())
            {
                // What may be read safely depends on the input relation (see
                // SetKnownInputOutputRelation in the public header). Looked up once
                // per round, so a change made through SendSetInputOutputRelation
                // takes effect from the next round.
                const int relation = knownRelation.load();
                const bool readRelays = (relation != static_cast<int>(InputOutputRelation::Momentary));
                const bool protectAfterInputEdge = (relation < 0); // only when the relation is unknown

                int queriesThisRound = 0;

                for (Channel ch = 1; ch <= ChannelCount; ++ch)
                {
                    if (!pollingActive.load())
                        return;

                    // Masks are re-read per channel so SetPollingMasks applies at once.
                    const ChannelMask bit = ChannelBit(ch);
                    const bool pollInput = (pollInputMask.load() & bit) != 0;
                    const bool pollRelay = (pollRelayMask.load() & bit) != 0;

                    DecodedResponse resp;

                    // The input is read FIRST: if it just changed, the board may have
                    // started a pulse on this relay by itself (input relation G3), and
                    // reading the relay now would cancel it. The relay read follows
                    // right away, so the unavoidable blind spot is only the ~20 ms
                    // between the two reads.
                    if (pollInput)
                    {
                        ++queriesThisRound;
                        OperationResult inputResult = Query(codec->EncodeReadInputStatus(ch),
                                                             ResponseKind::InputStatus, ch, resp, PollQueryTimeoutMs);
                        if (inputResult.IsOk() && PublishInputState(ch, resp.inputState) && protectAfterInputEdge)
                            ExtendQuietPeriod(ch, InputEdgeQuietMs);
                        if (!handle(inputResult, ch))
                            return;
                    }

                    if (!pollingActive.load())
                        return;

                    if (!pollRelay || !readRelays)
                        continue; // G3: a relay read could cancel a pulse the board started by itself

                    // A relay whose pulse is running (Momentary/Delay command, or an
                    // input edge just seen) must not be read: it would cancel the
                    // pulse. Skip it until the pulse is over.
                    if (!IsQuiet(ch))
                    {
                        ++queriesThisRound;
                        OperationResult relayResult = Query(codec->EncodeReadRelayStatus(ch),
                                                             ResponseKind::RelayStatus, ch, resp, PollQueryTimeoutMs);
                        if (relayResult.IsOk())
                            PublishRelayState(ch, resp.relayState);
                        if (!handle(relayResult, ch))
                            return;
                    }
                }

                // Pause between rounds, in small slices so that StopPolling()
                // returns promptly even with a long interval.
                // A round that sent nothing (empty masks, G3 with no inputs...) still
                // sleeps at least one slice, so the thread never spins.
                const int minimumPauseMs = (queriesThisRound == 0) ? 10 : 0;
                const int pauseMs = (pollIntervalMs > minimumPauseMs) ? pollIntervalMs : minimumPauseMs;
                for (int waited = 0; waited < pauseMs && pollingActive.load(); waited += 10)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    };

    Controller::Controller(ProtocolMode mode)
        : m_impl(std::make_unique<Impl>(mode))
    {
    }

    Controller::Controller(ProtocolMode mode, std::unique_ptr<ISerialTransport> transport)
        : m_impl(std::make_unique<Impl>(mode, std::move(transport)))
    {
    }

    Controller::~Controller()
    {
        StopPolling();
        Disconnect();
    }

    OperationResult Controller::Connect(const std::wstring& portName)
    {
        std::lock_guard<std::mutex> lock(m_impl->ioMutex);

        OperationResult result = m_impl->transport->Open(portName);
        if (result.IsOk())
        {
            m_impl->DrainInput();
            m_impl->ResetKnownStates(); // a new session: report everything again
            m_impl->knownRelation.store(-1); // the board's mode cannot be read back
        }
        return result;
    }

    void Controller::Disconnect()
    {
        std::lock_guard<std::mutex> lock(m_impl->ioMutex);
        m_impl->transport->Close();
    }

    bool Controller::IsConnected() const noexcept
    {
        return m_impl->transport->IsOpen();
    }

    OperationResult Controller::SendOpen(Channel channel)
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeOpen(channel), channel, false);
    }

    OperationResult Controller::SendClose(Channel channel)
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeClose(channel), channel, false);
    }

    OperationResult Controller::SendToggle(Channel channel)
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeToggle(channel), channel, false);
    }

    OperationResult Controller::SendLatch(Channel channel)
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeLatch(channel), channel, true);
    }

    OperationResult Controller::SendMomentary(Channel channel)
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeMomentary(channel), channel, false, MomentaryQuietMs);
    }

    OperationResult Controller::SendAllOpen()
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeAllOpen(), 0, false);
    }

    OperationResult Controller::SendAllClose()
    {
        return m_impl->SendRelayCommand(m_impl->codec->EncodeAllClose(), 0, false);
    }

    OperationResult Controller::SendSetInputOutputRelation(InputOutputRelation relation)
    {
        DecodedResponse reply;
        OperationResult result = m_impl->Query(m_impl->codec->EncodeSetInputOutputRelation(relation),
                                                ResponseKind::Acknowledge, 0, reply, CommandTimeoutMs);
        if (result.IsOk())
            SetKnownInputOutputRelation(relation); // the board confirmed: now both of us know
        return result;
    }

    void Controller::SetKnownInputOutputRelation(InputOutputRelation relation)
    {
        m_impl->knownRelation.store(static_cast<int>(relation));
    }

    OperationResult Controller::SendDelay(Channel channel, int seconds)
    {
        ByteBuffer cmd = m_impl->codec->EncodeDelay(channel, seconds);
        if (cmd.empty())
            return OperationResult::Error(ResultCode::InvalidChannel); // also covers "unsupported by this protocol"
        return m_impl->SendRelayCommand(cmd, channel, false, seconds * 1000 + DelayQuietExtraMs);
    }

    bool Controller::SupportsDelay() const noexcept
    {
        return m_impl->codec->SupportsDelay();
    }

    OperationResult Controller::QueryRelayStatus(Channel channel, RelayState& outState, int timeoutMs)
    {
        ByteBuffer cmd = m_impl->codec->EncodeReadRelayStatus(channel);
        DecodedResponse resp;
        OperationResult result = m_impl->Query(cmd, ResponseKind::RelayStatus, channel, resp, timeoutMs);
        if (result.IsOk())
            outState = resp.relayState;
        return result;
    }

    OperationResult Controller::QueryInputStatus(Channel channel, InputState& outState, int timeoutMs)
    {
        ByteBuffer cmd = m_impl->codec->EncodeReadInputStatus(channel);
        DecodedResponse resp;
        OperationResult result = m_impl->Query(cmd, ResponseKind::InputStatus, channel, resp, timeoutMs);
        if (result.IsOk())
            outState = resp.inputState;
        return result;
    }

    void Controller::SetEventCallback(EventCallback callback)
    {
        std::lock_guard<std::mutex> lock(m_impl->callbackMutex);
        m_impl->callback = std::move(callback);
    }

    void Controller::SetPollingMasks(ChannelMask relayMask, ChannelMask inputMask)
    {
        m_impl->pollRelayMask.store(relayMask);
        m_impl->pollInputMask.store(inputMask);
    }

    void Controller::StartPolling(int pollIntervalMs, ChannelMask relayMask, ChannelMask inputMask)
    {
        StopPolling();
        m_impl->ResetKnownStates(); // the first round reports every channel
        SetPollingMasks(relayMask, inputMask);
        m_impl->pollIntervalMs = pollIntervalMs;
        m_impl->pollingActive.store(true);
        m_impl->pollingThread = std::thread([this]() { m_impl->PollLoop(); });
    }

    void Controller::StopPolling()
    {
        m_impl->pollingActive.store(false);
        if (m_impl->pollingThread.joinable())
            m_impl->pollingThread.join();
    }

} // namespace UD85C08
