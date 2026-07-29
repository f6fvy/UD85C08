// UD85C08_MockSerialTransport.h
// Public API - in-memory fake of ISerialTransport.
//
// Lets UD85C08::Controller's logic (and any code built on top of it) be
// exercised without any physical serial port or UD85C08 board, by
// pre-scripting the bytes it should "receive" and inspecting the bytes
// it "sent". Intended for tests (ours and any consumer project's own
// tests) - not part of the normal runtime path.
//
// Usage sketch:
//   auto transport = std::make_unique<UD85C08::MockSerialTransport>();
//   auto* rawTransport = transport.get(); // keep a non-owning pointer for assertions
//   UD85C08::Controller controller(UD85C08::ProtocolMode::Hex, std::move(transport));
//   controller.Connect(L"MOCK"); // port name is ignored by the mock
//   rawTransport->QueueIncomingBytes({0x33,0x3C,0x00,0x00,0x00,0x01,0x01,0x71});
//   ...

#pragma once

#include "UD85C08_ISerialTransport.h"
#include <deque>
#include <vector>

namespace UD85C08
{
    class MockSerialTransport : public ISerialTransport
    {
    public:
        OperationResult Open(const std::wstring& /*portName*/) override
        {
            m_open = true;
            return OperationResult::Success();
        }

        void Close() override { m_open = false; }

        bool IsOpen() const noexcept override { return m_open; }

        OperationResult Write(const ByteBuffer& data) override
        {
            if (!m_open)
                return OperationResult::Error(ResultCode::NotConnected);

            // Record what was sent, for test assertions.
            m_sentHistory.push_back(data);
            return OperationResult::Success();
        }

        OperationResult Read(ByteBuffer& outData, size_t maxBytes, int /*timeoutMs*/) override
        {
            if (!m_open)
                return OperationResult::Error(ResultCode::NotConnected);

            if (m_rxQueue.empty())
                return OperationResult::Error(ResultCode::Timeout);

            outData.clear();
            while (!m_rxQueue.empty() && outData.size() < maxBytes)
            {
                outData.push_back(m_rxQueue.front());
                m_rxQueue.pop_front();
            }
            return OperationResult::Success();
        }

        // --- Test helpers (not part of ISerialTransport) -------------------

        // Queues bytes that will be returned by subsequent Read() calls,
        // simulating a response "sent" by the device.
        void QueueIncomingBytes(const ByteBuffer& bytes)
        {
            for (uint8_t b : bytes)
                m_rxQueue.push_back(b);
        }

        // All buffers previously passed to Write(), in call order.
        const std::vector<ByteBuffer>& GetSentHistory() const { return m_sentHistory; }

        void ClearHistory() { m_sentHistory.clear(); }

    private:
        bool m_open = false;
        std::deque<uint8_t> m_rxQueue;
        std::vector<ByteBuffer> m_sentHistory;
    };

} // namespace UD85C08
