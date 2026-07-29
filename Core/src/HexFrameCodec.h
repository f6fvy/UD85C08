// HexFrameCodec.h
// Internal to UD85C08_Core - implements IProtocolCodec for the 8-byte
// HEX protocol. Not exposed to external consumers of the library.
//
// Command frame layout (8 bytes):
//   [0]=0x55 [1]=0x56 [2..4]=0x00 0x00 0x00 [5]=channel [6]=command [7]=checksum
// Response frame layout (8 bytes):
//   [0]=0x33 [1]=0x3C [2..4]=0x00 0x00 0x00 [5]=channel [6]=command [7]=checksum
//
// The checksum is simply the sum of bytes [0..6] modulo 256. This was
// verified against every worked example in the datasheet (see the
// conversation / project notes) rather than hard-coded from the tables,
// so it is computed dynamically here.
//
// NOTE: the datasheet only documents response frames for the "relay
// open" (0x01) and "relay close" (0x02) commands. The response format
// for toggle/interlock/momentary/read-status/read-input is not
// documented and has not been verified against real hardware yet.
// TryDecode() below only recognizes open/close responses; anything
// else is reported as ResponseKind::Unrecognized until this can be
// confirmed with the physical device.

#pragma once

#include "UD85C08_IProtocolCodec.h"

namespace UD85C08
{
    class HexFrameCodec : public IProtocolCodec
    {
    public:
        ProtocolMode GetMode() const noexcept override { return ProtocolMode::Hex; }
        bool SupportsDelay() const noexcept override { return false; }

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

        // HEX protocol has no Delay command; always returns an empty buffer.
        ByteBuffer EncodeDelay(Channel channel, int seconds) const override;

        bool TryDecode(const ByteBuffer& buffer,
                        DecodedResponse& out,
                        size_t& bytesConsumed) const override;

    private:
        static constexpr uint8_t CommandHead1 = 0x55;
        static constexpr uint8_t CommandHead2 = 0x56;
        static constexpr uint8_t ResponseHead1 = 0x33;
        static constexpr uint8_t ResponseHead2 = 0x3C;

        static constexpr uint8_t CmdRead        = 0x00;
        static constexpr uint8_t CmdOpen        = 0x01;
        static constexpr uint8_t CmdClose       = 0x02;
        static constexpr uint8_t CmdToggle      = 0x03;
        static constexpr uint8_t CmdInterlock   = 0x04;
        static constexpr uint8_t CmdMomentary   = 0x05;
        static constexpr uint8_t CmdAllOpen     = 0x07;
        static constexpr uint8_t CmdAllClose    = 0x08;
        static constexpr uint8_t CmdReadInput   = 0x09;
        static constexpr uint8_t CmdSetIoRelation = 0x0A;

        // Builds a command frame: head + 3 reserved zero bytes + channelByte
        // + command + checksum (checksum computed as sum(bytes[0..6]) & 0xFF).
        static ByteBuffer BuildFrame(uint8_t channelByte, uint8_t command);

        static bool IsValidChannel(Channel channel);
    };

} // namespace UD85C08
