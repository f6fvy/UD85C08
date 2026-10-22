// SerialTransport.h
// Internal to UD85C08_Core - real ISerialTransport implementation,
// backed by CSerialPort2 (naughter.com, see third_party/naughter/).
//
// Uses CSerialPort2 in blocking (non-overlapped) mode: simplest possible
// integration, matching the synchronous nature of ISerialTransport::Read/
// Write. Controller's polling thread keeps the periodic reads off a GUI's
// message loop; a Send*/Query* call, on the other hand, blocks its caller
// for the duration of the exchange (about 20 ms on the real board, at most
// the command/query timeout if the board stays silent).
//
// Validated on the real board (open, read/write, timeouts, COM10+ names,
// unplug), in both 64-bit and 32-bit builds.

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
