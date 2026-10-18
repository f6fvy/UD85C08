// HexFrameCodec.cpp

#include "HexFrameCodec.h"

namespace UD85C08
{
    namespace
    {
        constexpr size_t FrameSize = 8;

        uint8_t ComputeChecksum(const ByteBuffer& frame /* first 7 bytes used */)
        {
            uint32_t sum = 0;
            for (size_t i = 0; i < 7; ++i)
                sum += frame[i];
            return static_cast<uint8_t>(sum & 0xFF);
        }
    }

    bool HexFrameCodec::IsValidChannel(Channel channel)
    {
        return channel >= 1 && channel <= ChannelCount;
    }

    ByteBuffer HexFrameCodec::BuildFrame(uint8_t channelByte, uint8_t command)
    {
        ByteBuffer frame(FrameSize, 0x00);
        frame[0] = CommandHead1;
        frame[1] = CommandHead2;
        // frame[2..4] stay 0x00 (reserved)
        frame[5] = channelByte;
        frame[6] = command;
        frame[7] = ComputeChecksum(frame);
        return frame;
    }

    ByteBuffer HexFrameCodec::EncodeReadRelayStatus(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdRead);
    }

    ByteBuffer HexFrameCodec::EncodeOpen(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdOpen);
    }

    ByteBuffer HexFrameCodec::EncodeClose(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdClose);
    }

    ByteBuffer HexFrameCodec::EncodeToggle(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdToggle);
    }

    ByteBuffer HexFrameCodec::EncodeLatch(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdInterlock);
    }

    ByteBuffer HexFrameCodec::EncodeMomentary(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdMomentary);
    }

    ByteBuffer HexFrameCodec::EncodeAllOpen() const
    {
        return BuildFrame(0x00, CmdAllOpen);
    }

    ByteBuffer HexFrameCodec::EncodeAllClose() const
    {
        return BuildFrame(0x00, CmdAllClose);
    }

    ByteBuffer HexFrameCodec::EncodeReadInputStatus(Channel channel) const
    {
        if (!IsValidChannel(channel)) return {};
        return BuildFrame(static_cast<uint8_t>(channel), CmdReadInput);
    }

    ByteBuffer HexFrameCodec::EncodeSetInputOutputRelation(InputOutputRelation relation) const
    {
        // Per datasheet, the relation value (0-3) takes the place of the
        // "channel" byte for this specific command.
        return BuildFrame(static_cast<uint8_t>(relation), CmdSetIoRelation);
    }

    ByteBuffer HexFrameCodec::EncodeDelay(Channel /*channel*/, int /*seconds*/) const
    {
        return {}; // not supported by the HEX protocol
    }

    bool HexFrameCodec::TryDecode(const ByteBuffer& buffer,
                                   DecodedResponse& out,
                                   size_t& bytesConsumed) const
    {
        bytesConsumed = 0;

        if (buffer.size() < FrameSize)
            return false; // need more bytes to have a full 8-byte frame

        const bool isResponse = (buffer[0] == ResponseHead1 && buffer[1] == ResponseHead2);
        const bool isEcho     = (buffer[0] == CommandHead1  && buffer[1] == CommandHead2);

        // Resynchronize if neither known header is present.
        if (!isResponse && !isEcho)
        {
            bytesConsumed = 1; // skip one byte and let the caller retry
            return false;
        }

        // Whether the checksum matches or not, this is a complete 8-byte
        // frame - consume it either way.
        bytesConsumed = FrameSize;
        out = DecodedResponse{};

        if (ComputeChecksum(buffer) != buffer[7])
        {
            out.kind = ResponseKind::Unrecognized;
            return true;
        }

        const uint8_t channelByte = buffer[5];
        const uint8_t command = buffer[6];
        out.channel = channelByte; // 0 means "all channels"

        if (isResponse)
        {
            switch (command)
            {
                case CmdOpen:
                    out.kind = ResponseKind::RelayStatus;
                    out.relayState = RelayState::Open;
                    break;
                case CmdClose:
                    out.kind = ResponseKind::RelayStatus;
                    out.relayState = RelayState::Closed;
                    break;
                case RespInputOn:
                    out.kind = ResponseKind::InputStatus;
                    out.inputState = InputState::On;
                    break;
                case RespInputOff:
                    out.kind = ResponseKind::InputStatus;
                    out.inputState = InputState::Off;
                    break;
                default:
                    out.kind = ResponseKind::Unrecognized;
                    break;
            }
        }
        else // echo of one of our own command frames
        {
            // Only the "set I/O relation" command is acknowledged this
            // way; 'channel' then carries the relation value (0..3).
            if (command == CmdSetIoRelation)
                out.kind = ResponseKind::Acknowledge;
            else
                out.kind = ResponseKind::Unrecognized;
        }

        return true;
    }

} // namespace UD85C08
