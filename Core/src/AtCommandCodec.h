// AtCommandCodec.h
// Internal to UD85C08_Core - implements IProtocolCodec for the AT (ASCII)
// protocol. Not exposed to external consumers of the library.
//
// Frame format: "AT+<CMD><channel>[=<value>]\r\n", uppercase only
// (per datasheet: "AT commands must be uppercase, lowercase invalid").
//
// Response format (per datasheet): lines such as "Open1", "Close3",
// "ON2", "OFF5", "OK1", terminated by CR/LF. The exact terminator used
// by the real device has not been verified against physical hardware
// yet; this codec accepts '\r', '\n', or "\r\n" as a line terminator
// when decoding, which should be robust to any of the three.

#pragma once

#include "UD85C08_IProtocolCodec.h"

namespace UD85C08
{
    class AtCommandCodec : public IProtocolCodec
    {
    public:
        ProtocolMode GetMode() const noexcept override { return ProtocolMode::At; }
        bool SupportsDelay() const noexcept override { return true; }

        ByteBuffer EncodeReadRelayStatus(Channel channel) const override;
        ByteBuffer EncodeOpen(Channel channel) const override;
        ByteBuffer EncodeClose(Channel channel) const override;
        ByteBuffer EncodeToggle(Channel channel) const override;
        ByteBuffer EncodeLatch(Channel channel) const override;
        ByteBuffer EncodeMomentary(Channel channel) const override;
        ByteBuffer EncodeAllOpen() const override;
        ByteBuffer EncodeAllClose() const override;
        ByteBuffer EncodeReadInputStatus(Channel channel) const override;
        ByteBuffer EncodeSetInputOutputRelation(InputOutputRelation relation) const override;
        ByteBuffer EncodeDelay(Channel channel, int seconds) const override;

        bool TryDecode(const ByteBuffer& buffer,
                        DecodedResponse& out,
                        size_t& bytesConsumed) const override;

    private:
        // Builds "AT+<letter><channel>\r\n", e.g. BuildSimple('O', 3) -> "AT+O3\r\n"
        static ByteBuffer BuildSimple(char letter, Channel channel);
    };

} // namespace UD85C08
