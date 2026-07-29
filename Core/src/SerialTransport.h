// SerialTransport.h
// Internal to UD85C08_Core - real ISerialTransport implementation,
// backed by CSerialPort2 (naughter.com, see third_party/naughter/).
//
// Uses CSerialPort2 in blocking (non-overlapped) mode: simplest possible
// integration, matching the synchronous nature of ISerialTransport::Read/
// Write. The higher-level worker thread inside Controller is what keeps
// this from blocking a GUI's message loop (see project notes / README).
//
// NOTE: this class has been written against the validated CSerialPort2
// API (see third_party/naughter/SerialPort.h) but has not yet been
// exercised against a physical UD85C08 board - no hardware was
// available at the time this was written. Treat the exact timeout
// behaviour as provisional until verified with real hardware.

#pragma once

#include "../include/UD85C08_ISerialTransport.h"
#include <memory>

namespace UD85C08
{
    class SerialTransport : public ISerialTransport
    {
    public:
        SerialTransport();
        ~SerialTransport() override;

        SerialTransport(const SerialTransport&) = delete;
        SerialTransport& operator=(const SerialTransport&) = delete;

        // portName example: L"COM5" (or L"\\\\.\\COM12" for ports above 9).
        // Fixed configuration required by the device: 9600 baud, 8 data
        // bits, no parity, 1 stop bit.
        OperationResult Open(const std::wstring& portName) override;

        void Close() override;
        bool IsOpen() const noexcept override;

        OperationResult Write(const ByteBuffer& data) override;
        OperationResult Read(ByteBuffer& outData, size_t maxBytes, int timeoutMs) override;

    private:
        // pImpl to keep CSerialPort2 (and therefore ATL) out of this header.
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace UD85C08
