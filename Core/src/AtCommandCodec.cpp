// AtCommandCodec.cpp

#include "AtCommandCodec.h"
#include <string>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace UD85C08
{
    namespace
    {
        ByteBuffer StringToBuffer(const std::string& s)
        {
            return ByteBuffer(s.begin(), s.end());
        }

        bool IsValidChannel(Channel channel)
        {
            return channel >= 1 && channel <= ChannelCount;
        }

        // Characters the device may emit around a response (see the header
        // comment): CR, LF, space and tab are all treated as padding.
        bool IsSeparator(uint8_t c)
        {
            return c == '\r' || c == '\n' || c == ' ' || c == '\t';
        }

        // A response is "<prefix><one channel character>". The prefix
        // determines what kind of information it carries.
        struct TokenSpec
        {
            const char*  prefix;
            ResponseKind kind;
            RelayState   relayState;
            InputState   inputState;
        };

        // None of these prefixes is a prefix of another one, so the first
        // full match is unambiguous.
        const TokenSpec Tokens[] =
        {
            { "Open",  ResponseKind::RelayStatus, RelayState::Open,    InputState::Unknown },
            { "Close", ResponseKind::RelayStatus, RelayState::Closed,  InputState::Unknown },
            { "OFF",   ResponseKind::InputStatus, RelayState::Unknown, InputState::Off     },
            { "ON",    ResponseKind::InputStatus, RelayState::Unknown, InputState::On      },
            { "OK",    ResponseKind::Acknowledge, RelayState::Unknown, InputState::Unknown },
        };

        enum class MatchResult
        {
            None,    // the bytes at 'pos' cannot start this token
            Partial, // they could, but more bytes are needed to tell
            Full,    // the whole prefix is present
        };

        // Case-insensitive comparison of the bytes starting at 'pos' with 'prefix'.
        MatchResult MatchPrefix(const ByteBuffer& buffer, size_t pos, const char* prefix)
        {
            const size_t prefixLen = std::strlen(prefix);
            const size_t available = buffer.size() - pos;
            const size_t compareCount = (available < prefixLen) ? available : prefixLen;

            for (size_t i = 0; i < compareCount; ++i)
            {
                if (std::toupper(buffer[pos + i]) != std::toupper(static_cast<unsigned char>(prefix[i])))
                    return MatchResult::None;
            }

            return (available >= prefixLen) ? MatchResult::Full : MatchResult::Partial;
        }
    } // namespace

    ByteBuffer AtCommandCodec::BuildSimple(char letter, Channel channel)
    {
        if (!IsValidChannel(channel))
            return {};

        std::string s = "AT+";
        s += letter;
        s += std::to_string(channel);
        return StringToBuffer(s);
    }

    ByteBuffer AtCommandCodec::EncodeReadRelayStatus(Channel channel) const { return BuildSimple('R', channel); }
    ByteBuffer AtCommandCodec::EncodeOpen(Channel channel)             const { return BuildSimple('O', channel); }
    ByteBuffer AtCommandCodec::EncodeClose(Channel channel)            const { return BuildSimple('C', channel); }
    ByteBuffer AtCommandCodec::EncodeToggle(Channel channel)           const { return BuildSimple('T', channel); }
    ByteBuffer AtCommandCodec::EncodeLatch(Channel channel)            const { return BuildSimple('L', channel); }
    ByteBuffer AtCommandCodec::EncodeMomentary(Channel channel)        const { return BuildSimple('M', channel); }
    ByteBuffer AtCommandCodec::EncodeReadInputStatus(Channel channel)  const { return BuildSimple('I', channel); }

    ByteBuffer AtCommandCodec::EncodeAllOpen() const
    {
        return StringToBuffer("AT+AO");
    }

    ByteBuffer AtCommandCodec::EncodeAllClose() const
    {
        return StringToBuffer("AT+AC");
    }

    ByteBuffer AtCommandCodec::EncodeSetInputOutputRelation(InputOutputRelation relation) const
    {
        std::string s = "AT+G";
        s += std::to_string(static_cast<int>(relation));
        return StringToBuffer(s);
    }

    ByteBuffer AtCommandCodec::EncodeDelay(Channel channel, int seconds) const
    {
        if (!IsValidChannel(channel))
            return {};
        if (seconds < 0 || seconds > 9999)
            return {};

        char buf[32];
        std::snprintf(buf, sizeof(buf), "AT+D%d=%04d", channel, seconds);
        return StringToBuffer(std::string(buf));
    }

    bool AtCommandCodec::TryDecode(const ByteBuffer& buffer,
                                    DecodedResponse& out,
                                    size_t& bytesConsumed) const
    {
        bytesConsumed = 0;

        // Skip padding in front of the response (the device sends a '\n'
        // before relay status lines, and may leave a trailing space/'\n'
        // from the previous response that arrives later).
        size_t pos = 0;
        while (pos < buffer.size() && IsSeparator(buffer[pos]))
            ++pos;

        if (pos == buffer.size())
        {
            bytesConsumed = pos; // only padding so far: drop it, wait for more
            return false;
        }

        const TokenSpec* matched = nullptr;
        bool partial = false;

        for (const TokenSpec& spec : Tokens)
        {
            MatchResult m = MatchPrefix(buffer, pos, spec.prefix);
            if (m == MatchResult::Full)
            {
                matched = &spec;
                break;
            }
            if (m == MatchResult::Partial)
                partial = true;
        }

        if (!matched)
        {
            if (partial)
            {
                bytesConsumed = pos; // could still become a valid token: wait for more bytes
                return false;
            }

            bytesConsumed = pos + 1; // not the start of any known response: skip one byte and resync
            return false;
        }

        const size_t channelPos = pos + std::strlen(matched->prefix);
        if (channelPos >= buffer.size())
        {
            bytesConsumed = pos; // prefix complete, channel character not received yet
            return false;
        }

        const uint8_t c = buffer[channelPos];
        int channel;
        if (c == 'A' || c == 'a')
            channel = 0; // "all channels"
        else if (c >= '0' && c <= '9')
            channel = c - '0';
        else
        {
            bytesConsumed = pos + 1; // malformed: skip one byte and resync
            return false;
        }

        // Swallow any padding that immediately follows the token; whatever
        // has not arrived yet will be skipped by the next call.
        size_t end = channelPos + 1;
        while (end < buffer.size() && IsSeparator(buffer[end]))
            ++end;

        out = DecodedResponse{};
        out.kind = matched->kind;
        out.channel = channel;
        out.relayState = matched->relayState;
        out.inputState = matched->inputState;
        bytesConsumed = end;
        return true;
    }

} // namespace UD85C08
