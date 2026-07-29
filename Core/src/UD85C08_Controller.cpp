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
        // queries/sends and the background polling thread all go through
        // this mutex, so a command and a poll never interleave on the wire.
        std::mutex ioMutex;

        EventCallback callback;
        std::mutex callbackMutex;

        std::thread pollingThread;
        std::atomic<bool> pollingActive{ false };
        int pollIntervalMs = 200;
        std::array<InputState, ChannelCount + 1> lastInputState{}; // index 1..8 used

        explicit Impl(ProtocolMode mode, std::unique_ptr<ISerialTransport> injectedTransport = nullptr)
        {
            if (mode == ProtocolMode::At)
                codec = std::make_unique<AtCommandCodec>();
            else
                codec = std::make_unique<HexFrameCodec>();

            transport = injectedTransport ? std::move(injectedTransport)
                                           : std::unique_ptr<ISerialTransport>(std::make_unique<SerialTransport>());

            lastInputState.fill(InputState::Unknown);
        }

        void RaiseEvent(const Event& ev)
        {
            std::lock_guard<std::mutex> lock(callbackMutex);
            if (callback)
                callback(ev);
        }

        // Writes a raw command, no response expected/awaited.
        OperationResult WriteRaw(const ByteBuffer& cmd)
        {
            if (cmd.empty())
                return OperationResult::Error(ResultCode::InvalidChannel);

            std::lock_guard<std::mutex> lock(ioMutex);

            if (!transport->IsOpen())
                return OperationResult::Error(ResultCode::NotConnected);

            return transport->Write(cmd);
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
                        break; // need more bytes, or just resynchronized - go read more

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

        void PollLoop()
        {
            while (pollingActive.load())
            {
                for (Channel ch = 1; ch <= ChannelCount && pollingActive.load(); ++ch)
                {
                    ByteBuffer cmd = codec->EncodeReadInputStatus(ch);
                    DecodedResponse resp;
                    OperationResult result = Query(cmd, ResponseKind::InputStatus, ch, resp, 300);

                    if (result.IsOk())
                    {
                        if (resp.inputState != lastInputState[ch])
                        {
                            lastInputState[ch] = resp.inputState;

                            Event ev;
                            ev.type = EventType::InputStateChanged;
                            ev.channel = ch;
                            ev.inputState = resp.inputState;
                            RaiseEvent(ev);
                        }
                    }
                    else if (result.code != ResultCode::Timeout)
                    {
                        Event ev;
                        ev.type = EventType::TransportError;
                        ev.channel = ch;
                        ev.errorCode = result.code;
                        RaiseEvent(ev);
                    }
                    // A timeout on one channel's poll is not escalated as an
                    // error event - it is treated as "no answer this round",
                    // to avoid flooding the callback if the device is briefly
                    // slow to respond. Persistent failures will still surface
                    // via TransportError once the link genuinely drops
                    // (transport->Write/Read returning a non-timeout error).

                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
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
        StopInputPolling();
        Disconnect();
    }

    OperationResult Controller::Connect(const std::wstring& portName)
    {
        std::lock_guard<std::mutex> lock(m_impl->ioMutex);
        return m_impl->transport->Open(portName);
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
        return m_impl->WriteRaw(m_impl->codec->EncodeOpen(channel));
    }

    OperationResult Controller::SendClose(Channel channel)
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeClose(channel));
    }

    OperationResult Controller::SendToggle(Channel channel)
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeToggle(channel));
    }

    OperationResult Controller::SendLatch(Channel channel)
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeLatch(channel));
    }

    OperationResult Controller::SendMomentary(Channel channel)
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeMomentary(channel));
    }

    OperationResult Controller::SendAllOpen()
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeAllOpen());
    }

    OperationResult Controller::SendAllClose()
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeAllClose());
    }

    OperationResult Controller::SendSetInputOutputRelation(InputOutputRelation relation)
    {
        return m_impl->WriteRaw(m_impl->codec->EncodeSetInputOutputRelation(relation));
    }

    OperationResult Controller::SendDelay(Channel channel, int seconds)
    {
        ByteBuffer cmd = m_impl->codec->EncodeDelay(channel, seconds);
        if (cmd.empty())
            return OperationResult::Error(ResultCode::InvalidChannel); // also covers "unsupported by this protocol"
        return m_impl->WriteRaw(cmd);
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

    void Controller::StartInputPolling(int pollIntervalMs)
    {
        StopInputPolling();
        m_impl->pollIntervalMs = pollIntervalMs;
        m_impl->pollingActive.store(true);
        m_impl->pollingThread = std::thread([this]() { m_impl->PollLoop(); });
    }

    void Controller::StopInputPolling()
    {
        m_impl->pollingActive.store(false);
        if (m_impl->pollingThread.joinable())
            m_impl->pollingThread.join();
    }

} // namespace UD85C08
