// UD85C08_ISerialTransport.h
// Public API - abstraction over the physical serial link.
//
// This is our own abstraction (no CSerialPort2 / ATL type appears here),
// exposed publicly for two reasons:
//   - It lets Controller be constructed with a custom transport, most
//     notably UD85C08_MockSerialTransport.h, so that command sequencing,
//     response parsing and event dispatch can be unit-tested without any
//     physical UD85C08 board attached.
//   - Any consumer project (CLI, GUI, or a future one) can reuse the same
//     mechanism for its own tests.
//
// UD85C08::SerialTransport (the real, CSerialPort2-backed implementation)
// is what UD85C08::Controller uses by default when constructed with just
// a ProtocolMode; passing a custom ISerialTransport overrides that.

#pragma once

#include "UD85C08_Types.h"
#include "UD85C08_IProtocolCodec.h" // for ByteBuffer
#include <string>

namespace UD85C08
{
    class ISerialTransport
    {
    public:
        virtual ~ISerialTransport() = default;

        // Opens the given COM port at 9600 baud, 8 data bits, no parity,
        // 1 stop bit (the fixed configuration required by the device).
        virtual OperationResult Open(const std::wstring& portName) = 0;

        virtual void Close() = 0;

        virtual bool IsOpen() const noexcept = 0;

        // Blocking write of the whole buffer.
        virtual OperationResult Write(const ByteBuffer& data) = 0;

        // Blocking read: waits up to timeoutMs for at least one byte to
        // become available, then reads whatever is currently available
        // (up to maxBytes) without blocking further. Returns Ok even if
        // fewer than maxBytes were read; the caller (Controller) is
        // responsible for accumulating bytes across calls until the
        // active codec recognizes a complete message.
        // Returns ResultCode::Timeout if nothing arrived within timeoutMs.
        virtual OperationResult Read(ByteBuffer& outData,
                                      size_t maxBytes,
                                      int timeoutMs) = 0;
    };

} // namespace UD85C08
