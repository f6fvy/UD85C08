// UD85C08_MockSerialTransport.h
// Public API - in-memory fake of ISerialTransport.
//
// Lets UD85C08::Controller's logic (and any code built on top of it) be
// exercised without any physical serial port or UD85C08 board. Two ways
// to script what the "device" says:
//   - QueueIncomingBytes(): bytes to be returned by the next Read() calls;
//   - SetResponder(): a function called on every Write() that returns the
//     bytes the device would answer with. This allows a stateful fake
//     board (see SelfTests.cpp) and is what makes polling testable.
//
// Behaves like a real port in the one respect that matters for timing:
// Read() with nothing to deliver really waits for the timeout before
// reporting ResultCode::Timeout.
//
// All methods are thread-safe, since Controller's polling thread and the
// test (or application) thread use the transport concurrently. The one
// exception is GetSentHistory(), which returns a reference: only read it
// while nothing else is using the transport (e.g. while not polling).
//
// Usage sketch:
//   auto transport = std::make_unique<UD85C08::MockSerialTransport>();
//   auto* mock = transport.get(); // keep a non-owning pointer for scripting
//   UD85C08::Controller controller(UD85C08::ProtocolMode::Hex, std::move(transport));
//   controller.Connect(L"MOCK"); // port name is ignored by the mock
//   mock->QueueIncomingBytes({0x33,0x3C,0x00,0x00,0x00,0x01,0x01,0x71});

#pragma once

#include "UD85C08_ISerialTransport.h"
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace UD85C08
{
    class MockSerialTransport : public ISerialTransport
    {
    public:
        // Called with each buffer written; returns the bytes the simulated
        // device answers with (may be empty).
        using Responder = std::function<ByteBuffer(const ByteBuffer& sent)>;

        OperationResult Open(const std::wstring& /*portName*/) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = true;
            return OperationResult::Success();
        }

        void Close() override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_open = false;
        }

        bool IsOpen() const noexcept override
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_open;
        }

        OperationResult Write(const ByteBuffer& data) override
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (!m_open)
                return OperationResult::Error(ResultCode::NotConnected);

            if (m_forcedError)
                return OperationResult::Error(m_forcedErrorCode);

            // Record what was sent, for test assertions.
            m_sentHistory.push_back(data);

            if (m_responder)
            {
                ByteBuffer reply = m_responder(data);
                for (uint8_t b : reply)
                    m_rxQueue.push_back(b);
            }

            return OperationResult::Success();
        }

        OperationResult Read(ByteBuffer& outData, size_t maxBytes, int timeoutMs) override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                if (!m_open)
                    return OperationResult::Error(ResultCode::NotConnected);

                if (m_forcedError)
                    return OperationResult::Error(m_forcedErrorCode);

                if (!m_rxQueue.empty())
                {
                    outData.clear();
                    while (!m_rxQueue.empty() && outData.size() < maxBytes)
                    {
                        outData.push_back(m_rxQueue.front());
                        m_rxQueue.pop_front();
                    }
                    return OperationResult::Success();
                }
            }

            // Nothing to deliver: wait the full timeout, like a real port
            // (done outside the lock so other threads are not blocked).
            if (timeoutMs > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));

            outData.clear();
            return OperationResult::Error(ResultCode::Timeout);
        }

        // --- Test helpers (not part of ISerialTransport) -------------------

        // Queues bytes that will be returned by subsequent Read() calls,
        // simulating a response "sent" by the device.
        void QueueIncomingBytes(const ByteBuffer& bytes)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (uint8_t b : bytes)
                m_rxQueue.push_back(b);
        }

        // Installs (or, with an empty function, removes) the responder.
        void SetResponder(Responder responder)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_responder = std::move(responder);
        }

        // Forces every subsequent Read()/Write() call to fail with the
        // given error, instead of behaving normally. Simulates a genuine
        // transport failure (e.g. the USB cable being unplugged), as
        // opposed to a mere "no data yet" timeout.
        void SetForcedError(ResultCode code)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_forcedError = true;
            m_forcedErrorCode = code;
        }

        void ClearForcedError()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_forcedError = false;
        }

        // All buffers previously passed to Write(), in call order. Not
        // thread-safe against concurrent Write() calls (see the top note).
        const std::vector<ByteBuffer>& GetSentHistory() const { return m_sentHistory; }

        void ClearHistory()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_sentHistory.clear();
        }

    private:
        mutable std::mutex m_mutex;
        bool m_open = false;
        bool m_forcedError = false;
        ResultCode m_forcedErrorCode = ResultCode::TransportError;
        std::deque<uint8_t> m_rxQueue;
        std::vector<ByteBuffer> m_sentHistory;
        Responder m_responder;
    };

} // namespace UD85C08
