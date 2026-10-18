// AtCommandCodec.h
// Internal to UD85C08_Core - implements IProtocolCodec for the AT (ASCII)
// protocol. Not exposed to external consumers of the library.
//
// Commands: "AT+<CMD><channel>[=<value>]", uppercase only (per datasheet:
// "AT commands must be uppercase, lowercase invalid"). Verified on real
// hardware: NO line terminator is required, so none is sent.
//
// Responses (verified on real hardware, see DEVICE_BEHAVIOR.md). The
// framing is irregular, which is why this codec does not rely on line
// terminators:
//   relay state : "\nOpen1 \n"   (note the trailing space) or "\nClose1\n"
//   all relays  : "\nOpenA \n"   or "\nCloseA\n"
//   input state : "ON1" / "OFF1" (no terminator at all)
//   G command   : "OK1"          (no terminator at all; the digit is the
//                                 G mode that was set, 0..3)
// A response is therefore recognised as "known prefix + one channel
// character" (a digit, or 'A' meaning all channels), ignoring any
// CR / LF / space / tab around it.

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
        // Builds "AT+<letter><channel>", e.g. BuildSimple('O', 3) -> "AT+O3"
        static ByteBuffer BuildSimple(char letter, Channel channel);
    };

} // namespace UD85C08
