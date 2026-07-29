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

        // Case-insensitive prefix check. Returns true and advances 'pos'
        // past the prefix if 'text' starts with 'prefix' at position 0.
        bool StartsWithNoCase(const std::string& text, const char* prefix)
        {
            size_t len = std::strlen(prefix);
            if (text.size() < len)
                return false;
            for (size_t i = 0; i < len; ++i)
            {
                if (std::toupper(static_cast<unsigned char>(text[i])) !=
                    std::toupper(static_cast<unsigned char>(prefix[i])))
                    return false;
            }
            return true;
        }

        // Parses the trailing digits (or 'A'/'a' meaning "all channels" -> 0)
        // found after a known prefix. Returns -1 if nothing valid follows.
        int ParseTrailingChannel(const std::string& text, size_t prefixLen)
        {
            if (prefixLen >= text.size())
                return -1;

            if (text[prefixLen] == 'A' || text[prefixLen] == 'a')
                return 0; // "all channels"

            int value = 0;
            bool any = false;
            for (size_t i = prefixLen; i < text.size(); ++i)
            {
                if (!std::isdigit(static_cast<unsigned char>(text[i])))
                    break;
                value = value * 10 + (text[i] - '0');
                any = true;
            }
            return any ? value : -1;
        }
    } // namespace

    ByteBuffer AtCommandCodec::BuildSimple(char letter, Channel channel)
    {
        if (!IsValidChannel(channel))
            return {};

        std::string s = "AT+";
        s += letter;
        s += std::to_string(channel);
        s += "\r\n";
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
        return StringToBuffer("AT+AO\r\n");
    }

    ByteBuffer AtCommandCodec::EncodeAllClose() const
    {
        return StringToBuffer("AT+AC\r\n");
    }

    ByteBuffer AtCommandCodec::EncodeSetInputOutputRelation(InputOutputRelation relation) const
    {
        std::string s = "AT+G";
        s += std::to_string(static_cast<int>(relation));
        s += "\r\n";
        return StringToBuffer(s);
    }

    ByteBuffer AtCommandCodec::EncodeDelay(Channel channel, int seconds) const
    {
        if (!IsValidChannel(channel))
            return {};
        if (seconds < 0 || seconds > 9999)
            return {};

        char buf[32];
        std::snprintf(buf, sizeof(buf), "AT+D%d=%04d\r\n", channel, seconds);
        return StringToBuffer(std::string(buf));
    }

    bool AtCommandCodec::TryDecode(const ByteBuffer& buffer,
                                    DecodedResponse& out,
                                    size_t& bytesConsumed) const
    {
        bytesConsumed = 0;

        // Find a line terminator ('\r' or '\n'). If none yet, we need more data.
        size_t lineEnd = buffer.size();
        for (size_t i = 0; i < buffer.size(); ++i)
        {
            if (buffer[i] == '\r' || buffer[i] == '\n')
            {
                lineEnd = i;
                break;
            }
        }
        if (lineEnd == buffer.size())
            return false; // no full line yet

        std::string line(reinterpret_cast<const char*>(buffer.data()), lineEnd);

        // Consume the line plus any immediately following CR/LF characters
        // (handles "\r\n", "\n\r", or lone "\r"/"\n").
        size_t consumed = lineEnd;
        while (consumed < buffer.size() &&
               (buffer[consumed] == '\r' || buffer[consumed] == '\n'))
        {
            ++consumed;
        }
        bytesConsumed = consumed;

        if (line.empty())
        {
            out = DecodedResponse{};
            return true; // empty line, nothing to report but fully consumed
        }

        // Order matters: "OFF" must be checked before "ON" (both start with 'O').
        if (StartsWithNoCase(line, "Open"))
        {
            int ch = ParseTrailingChannel(line, 4);
            if (ch >= 0)
            {
                out.kind = ResponseKind::RelayStatus;
                out.channel = ch;
                out.relayState = RelayState::Open;
                return true;
            }
        }
        else if (StartsWithNoCase(line, "Close"))
        {
            int ch = ParseTrailingChannel(line, 5);
            if (ch >= 0)
            {
                out.kind = ResponseKind::RelayStatus;
                out.channel = ch;
                out.relayState = RelayState::Closed;
                return true;
            }
        }
        else if (StartsWithNoCase(line, "OFF"))
        {
            int ch = ParseTrailingChannel(line, 3);
            if (ch >= 0)
            {
                out.kind = ResponseKind::InputStatus;
                out.channel = ch;
                out.inputState = InputState::Off;
                return true;
            }
        }
        else if (StartsWithNoCase(line, "ON"))
        {
            int ch = ParseTrailingChannel(line, 2);
            if (ch >= 0)
            {
                out.kind = ResponseKind::InputStatus;
                out.channel = ch;
                out.inputState = InputState::On;
                return true;
            }
        }
        else if (StartsWithNoCase(line, "OK"))
        {
            int ch = ParseTrailingChannel(line, 2);
            if (ch >= 0)
            {
                out.kind = ResponseKind::Acknowledge;
                out.channel = ch;
                return true;
            }
        }

        // Unrecognized line - still fully consumed, reported as such so the
        // caller can decide whether to log/ignore it.
        out = DecodedResponse{};
        out.kind = ResponseKind::Unrecognized;
        return true;
    }

} // namespace UD85C08
