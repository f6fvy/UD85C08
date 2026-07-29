// UD85C08_IProtocolCodec.h
// Public API - interface shared by both protocol codecs (AT / HEX).
// Keeps UD85C08::Controller agnostic of which wire protocol is in use.
//
// A codec has two responsibilities:
//   1) Encode a high-level command (open channel 3, read input 5, ...)
//      into the raw bytes to send on the wire.
//   2) Decode raw bytes received from the device into a structured
//      DecodedResponse, consuming only as many bytes as one complete
//      message needs (AT responses are line-based, HEX responses are
//      fixed 8-byte frames - the codec hides that difference).

#pragma once

#include "UD85C08_Types.h"
#include <vector>
#include <cstdint>

namespace UD85C08
{
    // Kind of information carried by a decoded response.
    enum class ResponseKind
    {
        Unrecognized,
        RelayStatus,     // reports the state of a relay output
        InputStatus,     // reports the state of a digital input
        Acknowledge,      // generic "command accepted" (e.g. AT+G0 -> OK0)
    };

    struct DecodedResponse
    {
        ResponseKind kind = ResponseKind::Unrecognized;
        Channel      channel = 0;               // 0 means "all channels" / not applicable
        RelayState   relayState = RelayState::Unknown;
        InputState   inputState = InputState::Unknown;
    };

    // Raw byte buffer, used both for ASCII (AT) and binary (HEX) payloads.
    using ByteBuffer = std::vector<uint8_t>;

    class IProtocolCodec
    {
    public:
        virtual ~IProtocolCodec() = default;

        // Which protocol this codec implements.
        virtual ProtocolMode GetMode() const noexcept = 0;

        // Whether this codec supports the Delay command (AT only, per datasheet).
        virtual bool SupportsDelay() const noexcept = 0;

        // --- Encoding -----------------------------------------------------
        virtual ByteBuffer EncodeReadRelayStatus(Channel channel) const = 0;
        virtual ByteBuffer EncodeOpen(Channel channel) const = 0;
        virtual ByteBuffer EncodeClose(Channel channel) const = 0;
        virtual ByteBuffer EncodeToggle(Channel channel) const = 0;
        virtual ByteBuffer EncodeLatch(Channel channel) const = 0;
        virtual ByteBuffer EncodeMomentary(Channel channel) const = 0;
        virtual ByteBuffer EncodeAllOpen() const = 0;
        virtual ByteBuffer EncodeAllClose() const = 0;
        virtual ByteBuffer EncodeReadInputStatus(Channel channel) const = 0;
        virtual ByteBuffer EncodeSetInputOutputRelation(InputOutputRelation relation) const = 0;

        // Only valid when SupportsDelay() is true. seconds must be in [0, 9999].
        // Returns an empty buffer if the codec does not support this command.
        virtual ByteBuffer EncodeDelay(Channel channel, int seconds) const = 0;

        // --- Decoding -------------------------------------------------------
        // Attempts to decode one complete message starting at the beginning
        // of 'buffer'. On success, returns true, fills 'out', and sets
        // 'bytesConsumed' to the number of bytes that made up that message
        // (the caller is expected to erase/advance past them).
        // Returns false if 'buffer' does not yet contain a full message
        // (caller should wait for more bytes) - 'bytesConsumed' is then
        // set to 0. If the leading bytes are clearly invalid (bad checksum,
        // unrecognized syntax), the codec may set 'bytesConsumed' to a
        // small non-zero value to let the caller resynchronize by skipping
        // that many bytes.
        virtual bool TryDecode(const ByteBuffer& buffer,
                                DecodedResponse& out,
                                size_t& bytesConsumed) const = 0;
    };

} // namespace UD85C08
