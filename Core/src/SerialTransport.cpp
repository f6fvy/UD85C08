// SerialTransport.cpp

#include "SerialTransport.h"
#include "../third_party/naughter/SerialPort.h"

namespace UD85C08
{
    struct SerialTransport::Impl
    {
        CSerialPort2 port;
    };

    SerialTransport::SerialTransport()
        : m_impl(std::make_unique<Impl>())
    {
    }

    SerialTransport::~SerialTransport()
    {
        Close();
    }

    OperationResult SerialTransport::Open(const std::wstring& portName)
    {
        if (IsOpen())
            Close();

        // Fixed link parameters required by the UD85C08 datasheet:
        // 9600 baud, 8 data bits, no parity, 1 stop bit, no flow control.
        // bOverlapped = FALSE: blocking mode, see the note in SerialTransport.h.
        BOOL ok = m_impl->port.Open(
            portName.c_str(),
            9600,
            CSerialPort2::Parity::NoParity,
            8,
            CSerialPort2::StopBits::OneStopBit,
            CSerialPort2::FlowControl::NoFlowControl,
            FALSE);

        if (!ok)
            return OperationResult::Error(ResultCode::TransportError);

        return OperationResult::Success();
    }

    void SerialTransport::Close()
    {
        if (m_impl && m_impl->port.IsOpen())
            m_impl->port.Close();
    }

    bool SerialTransport::IsOpen() const noexcept
    {
        return m_impl && m_impl->port.IsOpen();
    }

    OperationResult SerialTransport::Write(const ByteBuffer& data)
    {
        if (!IsOpen())
            return OperationResult::Error(ResultCode::NotConnected);

        if (data.empty())
            return OperationResult::Success();

        DWORD bytesWritten = 0;
        BOOL ok = m_impl->port.Write(data.data(), static_cast<DWORD>(data.size()), bytesWritten);

        if (!ok || bytesWritten != data.size())
            return OperationResult::Error(ResultCode::TransportError);

        return OperationResult::Success();
    }

    OperationResult SerialTransport::Read(ByteBuffer& outData, size_t maxBytes, int timeoutMs)
    {
        if (!IsOpen())
            return OperationResult::Error(ResultCode::NotConnected);

        // Configure the read timeout for this call. Per the Win32 docs,
        // setting ReadIntervalTimeout = MAXDWORD together with
        // ReadTotalTimeoutMultiplier = 0 and a non-zero
        // ReadTotalTimeoutConstant makes ReadFile return immediately if
        // data is already available, otherwise wait up to that many
        // milliseconds before giving up.
        COMMTIMEOUTS timeouts{};
        timeouts.ReadIntervalTimeout = MAXDWORD;
        timeouts.ReadTotalTimeoutMultiplier = 0;
        timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(timeoutMs);
        m_impl->port.SetTimeouts(timeouts);

        outData.resize(maxBytes);
        DWORD bytesRead = 0;
        BOOL ok = m_impl->port.Read(outData.data(), static_cast<DWORD>(maxBytes), bytesRead);

        if (!ok)
        {
            outData.clear();
            return OperationResult::Error(ResultCode::TransportError);
        }

        outData.resize(bytesRead);

        if (bytesRead == 0)
            return OperationResult::Error(ResultCode::Timeout);

        return OperationResult::Success();
    }

} // namespace UD85C08
