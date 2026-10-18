// SelfTests.cpp
// Implements UD85C08::RunSelfTests(). Internal file, but the function it
// implements is declared in the public UD85C08_SelfTest.h so that CLI/GUI
// can call it without needing access to the internal codec headers.

#include "../include/UD85C08_SelfTest.h"
#include "../include/UD85C08_Controller.h"
#include "../include/UD85C08_MockSerialTransport.h"
#include "AtCommandCodec.h"
#include "HexFrameCodec.h"
#include "PortName.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>
#include <array>
#include <memory>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <chrono>
#include <thread>

namespace UD85C08
{
    namespace
    {
        int g_testCount = 0;
        int g_failCount = 0;

        void Check(bool condition, const std::string& testName)
        {
            ++g_testCount;
            if (condition)
            {
                std::cout << "[PASS] " << testName << "\n";
            }
            else
            {
                ++g_failCount;
                std::cout << "[FAIL] " << testName << "\n";
            }
        }

        bool BufferEquals(const ByteBuffer& actual, const std::vector<uint8_t>& expected)
        {
            return actual.size() == expected.size() &&
                   std::equal(actual.begin(), actual.end(), expected.begin());
        }

        bool BufferEqualsString(const ByteBuffer& actual, const std::string& expected)
        {
            return BufferEquals(actual, ByteBuffer(expected.begin(), expected.end()));
        }

        void TestHexEncoding()
        {
            HexFrameCodec codec;

            // Values taken verbatim from the "5 Control commands" section
            // of the datasheet.
            Check(BufferEquals(codec.EncodeReadRelayStatus(1), { 0x55,0x56,0x00,0x00,0x00,0x01,0x00,0xAC }),
                  "HEX: read status channel 1");
            Check(BufferEquals(codec.EncodeOpen(1), { 0x55,0x56,0x00,0x00,0x00,0x01,0x01,0xAD }),
                  "HEX: open channel 1");
            Check(BufferEquals(codec.EncodeOpen(8), { 0x55,0x56,0x00,0x00,0x00,0x08,0x01,0xB4 }),
                  "HEX: open channel 8");
            Check(BufferEquals(codec.EncodeClose(3), { 0x55,0x56,0x00,0x00,0x00,0x03,0x02,0xB0 }),
                  "HEX: close channel 3");
            Check(BufferEquals(codec.EncodeToggle(5), { 0x55,0x56,0x00,0x00,0x00,0x05,0x03,0xB3 }),
                  "HEX: toggle channel 5");
            Check(BufferEquals(codec.EncodeLatch(2), { 0x55,0x56,0x00,0x00,0x00,0x02,0x04,0xB1 }),
                  "HEX: latch (interlock) channel 2");
            Check(BufferEquals(codec.EncodeMomentary(8), { 0x55,0x56,0x00,0x00,0x00,0x08,0x05,0xB8 }),
                  "HEX: momentary channel 8");
            Check(BufferEquals(codec.EncodeAllOpen(), { 0x55,0x56,0x00,0x00,0x00,0x00,0x07,0xB2 }),
                  "HEX: all relays open");
            Check(BufferEquals(codec.EncodeAllClose(), { 0x55,0x56,0x00,0x00,0x00,0x00,0x08,0xB3 }),
                  "HEX: all relays close");
            Check(BufferEquals(codec.EncodeReadInputStatus(8), { 0x55,0x56,0x00,0x00,0x00,0x08,0x09,0xBC }),
                  "HEX: read input status channel 8");

            // Invalid channel -> empty buffer expected
            Check(codec.EncodeOpen(9).empty(), "HEX: invalid channel (9) yields empty buffer");
            Check(codec.EncodeOpen(0).empty(), "HEX: invalid channel (0) yields empty buffer");

            // Delay is not supported by the HEX protocol
            Check(!codec.SupportsDelay(), "HEX: SupportsDelay() is false");
            Check(codec.EncodeDelay(1, 10).empty(), "HEX: EncodeDelay yields empty buffer");
        }

        void TestHexDecoding()
        {
            HexFrameCodec codec;

            // "Return relay open" / "Return relay close" examples from the
            // datasheet's "2 return command" section.
            ByteBuffer openCh1 = { 0x33,0x3C,0x00,0x00,0x00,0x01,0x01,0x71 };
            DecodedResponse resp;
            size_t consumed = 0;
            bool decoded = codec.TryDecode(openCh1, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::RelayStatus &&
                  resp.channel == 1 && resp.relayState == RelayState::Open,
                  "HEX: decode 'return relay open' channel 1");

            ByteBuffer closeCh8 = { 0x33,0x3C,0x00,0x00,0x00,0x08,0x02,0x79 };
            decoded = codec.TryDecode(closeCh8, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::RelayStatus &&
                  resp.channel == 8 && resp.relayState == RelayState::Closed,
                  "HEX: decode 'return relay close' channel 8");

            // Incomplete frame -> needs more data
            ByteBuffer partial = { 0x33,0x3C,0x00,0x00 };
            decoded = codec.TryDecode(partial, resp, consumed);
            Check(!decoded && consumed == 0, "HEX: incomplete frame reports 'need more data'");

            // Corrupted checksum -> recognized as a full but unrecognized frame
            ByteBuffer badChecksum = { 0x33,0x3C,0x00,0x00,0x00,0x01,0x01,0x00 };
            decoded = codec.TryDecode(badChecksum, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::Unrecognized,
                  "HEX: bad checksum reported as Unrecognized (still consumed)");

            // --- Frames measured on a physical board (see DEVICE_BEHAVIOR.md) ---
            // Read input, input floating (not tied to ground) -> command 0x82
            ByteBuffer inputOff = { 0x33,0x3C,0x00,0x00,0x00,0x01,0x82,0xF2 };
            decoded = codec.TryDecode(inputOff, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::InputStatus &&
                  resp.channel == 1 && resp.inputState == InputState::Off,
                  "HEX (real): read input, floating input -> InputStatus Off");

            // Read input, input tied to ground -> command 0x81
            ByteBuffer inputOn = { 0x33,0x3C,0x00,0x00,0x00,0x01,0x81,0xF1 };
            decoded = codec.TryDecode(inputOn, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::InputStatus &&
                  resp.channel == 1 && resp.inputState == InputState::On,
                  "HEX (real): read input, grounded input -> InputStatus On");

            // "Set I/O relation" is acknowledged by echoing the command frame (header 55 56)
            ByteBuffer ioRelationEcho = { 0x55,0x56,0x00,0x00,0x00,0x01,0x0A,0xB6 };
            decoded = codec.TryDecode(ioRelationEcho, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::Acknowledge && resp.channel == 1,
                  "HEX (real): set I/O relation is acknowledged by an echo of the command");

            // An echoed frame for any other command is not a meaningful response
            ByteBuffer otherEcho = { 0x55,0x56,0x00,0x00,0x00,0x01,0x01,0xAD };
            decoded = codec.TryDecode(otherEcho, resp, consumed);
            Check(decoded && consumed == 8 && resp.kind == ResponseKind::Unrecognized,
                  "HEX: an echoed frame for any other command is reported as Unrecognized");

            // Junk byte in front of a valid frame -> skip one byte, then decode normally
            ByteBuffer junkThenFrame = { 0x00,0x33,0x3C,0x00,0x00,0x00,0x01,0x01,0x71 };
            decoded = codec.TryDecode(junkThenFrame, resp, consumed);
            Check(!decoded && consumed == 1, "HEX: a junk byte in front of a frame is skipped to resynchronize");
        }

        void TestAtEncoding()
        {
            AtCommandCodec codec;

            Check(BufferEqualsString(codec.EncodeOpen(3), "AT+O3"), "AT: open channel 3");
            Check(BufferEqualsString(codec.EncodeClose(8), "AT+C8"), "AT: close channel 8");
            Check(BufferEqualsString(codec.EncodeToggle(1), "AT+T1"), "AT: toggle channel 1");
            Check(BufferEqualsString(codec.EncodeLatch(2), "AT+L2"), "AT: latch channel 2");
            Check(BufferEqualsString(codec.EncodeMomentary(5), "AT+M5"), "AT: momentary channel 5");
            Check(BufferEqualsString(codec.EncodeAllOpen(), "AT+AO"), "AT: all relays open");
            Check(BufferEqualsString(codec.EncodeAllClose(), "AT+AC"), "AT: all relays close");
            Check(BufferEqualsString(codec.EncodeReadInputStatus(4), "AT+I4"), "AT: read input channel 4");
            Check(BufferEqualsString(codec.EncodeSetInputOutputRelation(InputOutputRelation::Interlocking), "AT+G2"),
                  "AT: set I/O relation G2");

            // Datasheet examples: "AT+D1=0010" and "AT+D2=0100"
            Check(BufferEqualsString(codec.EncodeDelay(1, 10), "AT+D1=0010"), "AT: delay channel 1, 10s");
            Check(BufferEqualsString(codec.EncodeDelay(2, 100), "AT+D2=0100"), "AT: delay channel 2, 100s");

            Check(codec.SupportsDelay(), "AT: SupportsDelay() is true");
            Check(codec.EncodeDelay(1, 10000).empty(), "AT: delay out of range (10000) yields empty buffer");
            Check(codec.EncodeOpen(9).empty(), "AT: invalid channel (9) yields empty buffer");
        }

        void TestAtDecoding()
        {
            AtCommandCodec codec;

            auto decodeText = [&](const std::string& text) {
                ByteBuffer buf(text.begin(), text.end());
                DecodedResponse resp;
                size_t consumed = 0;
                bool decoded = codec.TryDecode(buf, resp, consumed);
                return std::make_tuple(decoded, consumed, resp);
            };

            // --- Real framing, as measured on a physical board ---------------
            {
                auto [decoded, consumed, resp] = decodeText("\nOpen1 \n");
                Check(decoded && consumed == 8 && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 1 && resp.relayState == RelayState::Open,
                      "AT (real): '\\nOpen1 \\n' (leading LF, trailing space)");
            }
            {
                auto [decoded, consumed, resp] = decodeText("\nClose1\n");
                Check(decoded && consumed == 8 && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 1 && resp.relayState == RelayState::Closed,
                      "AT (real): '\\nClose1\\n'");
            }
            {
                auto [decoded, consumed, resp] = decodeText("\nOpenA \n");
                Check(decoded && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 0 && resp.relayState == RelayState::Open,
                      "AT (real): '\\nOpenA \\n' = all relays open (channel 0)");
            }
            {
                auto [decoded, consumed, resp] = decodeText("\nCloseA\n");
                Check(decoded && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 0 && resp.relayState == RelayState::Closed,
                      "AT (real): '\\nCloseA\\n' = all relays closed (channel 0)");
            }
            {
                auto [decoded, consumed, resp] = decodeText("OFF1");
                Check(decoded && consumed == 4 && resp.kind == ResponseKind::InputStatus &&
                      resp.channel == 1 && resp.inputState == InputState::Off,
                      "AT (real): 'OFF1' with NO terminator");
            }
            {
                auto [decoded, consumed, resp] = decodeText("ON8");
                Check(decoded && consumed == 3 && resp.kind == ResponseKind::InputStatus &&
                      resp.channel == 8 && resp.inputState == InputState::On,
                      "AT (real): 'ON8' with NO terminator");
            }
            {
                auto [decoded, consumed, resp] = decodeText("OK1");
                Check(decoded && consumed == 3 && resp.kind == ResponseKind::Acknowledge && resp.channel == 1,
                      "AT (real): 'OK1' with NO terminator (G mode acknowledgement)");
            }

            // --- Terminated forms are still accepted ---------------------------
            {
                auto [decoded, consumed, resp] = decodeText("Open1\r\n");
                Check(decoded && consumed == 7 && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 1 && resp.relayState == RelayState::Open,
                      "AT: 'Open1\\r\\n' with a CR/LF terminator");
            }

            // --- Back-to-back messages -------------------------------------------
            {
                // "\nOpen1 \n" immediately followed by "\nClose2\n"
                std::string text = "\nOpen1 \n\nClose2\n";
                ByteBuffer buf(text.begin(), text.end());
                DecodedResponse first, second;
                size_t consumed1 = 0, consumed2 = 0;

                bool ok1 = codec.TryDecode(buf, first, consumed1);
                buf.erase(buf.begin(), buf.begin() + consumed1);
                bool ok2 = codec.TryDecode(buf, second, consumed2);

                Check(ok1 && first.channel == 1 && first.relayState == RelayState::Open &&
                      ok2 && second.channel == 2 && second.relayState == RelayState::Closed,
                      "AT: two back-to-back messages are decoded one after the other");
            }
            {
                // Padding left over from a previous response is skipped, not misread.
                auto [decoded, consumed, resp] = decodeText(" \nOFF3");
                Check(decoded && resp.kind == ResponseKind::InputStatus && resp.channel == 3 &&
                      resp.inputState == InputState::Off,
                      "AT: leading padding in front of a response is skipped");
            }

            // --- Incomplete data: wait for more -------------------------------------
            {
                auto [decoded, consumed, resp] = decodeText("Ope");
                Check(!decoded && consumed == 0, "AT: partial prefix 'Ope' reports 'need more data'");
            }
            {
                auto [decoded, consumed, resp] = decodeText("Open");
                Check(!decoded && consumed == 0, "AT: prefix without its channel character reports 'need more data'");
            }
            {
                auto [decoded, consumed, resp] = decodeText("OF");
                Check(!decoded && consumed == 0, "AT: partial prefix 'OF' reports 'need more data'");
            }
            {
                auto [decoded, consumed, resp] = decodeText("\n");
                Check(!decoded && consumed == 1, "AT: lone padding is dropped while waiting for more");
            }

            // --- Garbage: resynchronize ----------------------------------------------
            {
                auto [decoded, consumed, resp] = decodeText("Xyz");
                Check(!decoded && consumed == 1, "AT: garbage byte is skipped to resynchronize");
            }
        }

        void TestPortName()
        {
            Check(NormalizePortName(L"COM6")  == L"\\\\.\\COM6",  "Port name: 'COM6' gets the extended prefix");
            Check(NormalizePortName(L"COM12") == L"\\\\.\\COM12", "Port name: 'COM12' gets the extended prefix (bare form fails on Windows)");
            Check(NormalizePortName(L"\\\\.\\COM12") == L"\\\\.\\COM12",
                  "Port name: an already extended name is left unchanged");
            Check(NormalizePortName(L"").empty(), "Port name: an empty name stays empty");
        }

        // ---- Helpers for the Controller tests ---------------------------------

        ByteBuffer Text(const std::string& s)
        {
            return ByteBuffer(s.begin(), s.end());
        }

        // Frame sent back by the real board: 33 3C 00 00 00 <channel> <command> <checksum>
        ByteBuffer HexResponse(int channel, uint8_t command)
        {
            ByteBuffer f = { 0x33, 0x3C, 0x00, 0x00, 0x00, static_cast<uint8_t>(channel), command, 0x00 };
            uint32_t sum = 0;
            for (int i = 0; i < 7; ++i)
                sum += f[i];
            f[7] = static_cast<uint8_t>(sum & 0xFF);
            return f;
        }

        // Replies in the exact formats measured on a real board (see DEVICE_BEHAVIOR.md).
        ByteBuffer RelayReply(ProtocolMode mode, int channel /* 0 = all */, bool open)
        {
            if (mode == ProtocolMode::Hex)
                return HexResponse(channel, open ? 0x01 : 0x02);

            std::string s = open ? "\nOpen" : "\nClose";
            s += (channel == 0) ? 'A' : static_cast<char>('0' + channel);
            s += open ? " \n" : "\n";
            return Text(s);
        }

        ByteBuffer InputReply(ProtocolMode mode, int channel, bool on)
        {
            if (mode == ProtocolMode::Hex)
                return HexResponse(channel, on ? 0x81 : 0x82);

            std::string s = on ? "ON" : "OFF";
            s += static_cast<char>('0' + channel);
            return Text(s);
        }

        // A tiny simulated board: keeps 8 relay and 8 input states and answers
        // every command in the real formats. SetRelay / SetInput change a state
        // "from outside", silently - like a physical input would.
        //
        // It also reproduces a behaviour measured on the real board: Momentary
        // (1.0 s) and Delay pulses run on a real clock, and READING the state of
        // a relay while its pulse is running CANCELS the pulse (the relay then
        // stays on). Reads of inputs or of other relays are harmless.
        class FakeBoard
        {
        public:
            explicit FakeBoard(ProtocolMode mode)
                : m_mode(mode)
            {
                if (mode == ProtocolMode::At)
                    m_codec = std::make_unique<AtCommandCodec>();
                else
                    m_codec = std::make_unique<HexFrameCodec>();

                m_relay.fill(false);
                m_input.fill(false);
                m_pulsing.fill(false);
            }

            // What the board does to a relay when its input goes ON (the "input
            // relations"): nothing (G0), toggle it (G1-like), or start a 0.5 s
            // pulse on the edge (G3-like; measured on the board: it lasts 0.9 to
            // 1.3 s and falls back even while the input stays grounded).
            enum class InputAction { None, Toggle, Latch, Pulse };
            void SetInputAction(InputAction action) { std::lock_guard<std::mutex> l(m_mutex); m_inputAction = action; }

            void SetRelay(int ch, bool open) { std::lock_guard<std::mutex> l(m_mutex); m_relay[ch] = open; m_pulsing[ch] = false; }

            void SetInput(int ch, bool on)
            {
                std::lock_guard<std::mutex> l(m_mutex);
                ApplyPulseEnds();
                const bool risingEdge = on && !m_input[ch];
                m_input[ch] = on;

                if (risingEdge && m_inputAction == InputAction::Toggle)
                {
                    m_relay[ch] = !m_relay[ch];
                    m_pulsing[ch] = false;
                }
                else if (risingEdge && m_inputAction == InputAction::Latch)
                {
                    for (int other = 1; other <= ChannelCount; ++other)
                    {
                        m_relay[other] = (other == ch);
                        m_pulsing[other] = false;
                    }
                }
                else if (risingEdge && m_inputAction == InputAction::Pulse)
                {
                    StartPulse(ch, 1200);
                }
            }
            bool GetRelay(int ch)            { std::lock_guard<std::mutex> l(m_mutex); ApplyPulseEnds(); return m_relay[ch]; }

            // How many times the controller asked for a relay state / an input state.
            int RelayReads() const { return m_relayReads.load(); }
            int InputReads() const { return m_inputReads.load(); }

            ByteBuffer Respond(const ByteBuffer& sent)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ApplyPulseEnds();

                if (sent == m_codec->EncodeAllOpen())
                {
                    for (int ch = 1; ch <= ChannelCount; ++ch) { m_relay[ch] = true; m_pulsing[ch] = false; }
                    return RelayReply(m_mode, 0, true);
                }
                if (sent == m_codec->EncodeAllClose())
                {
                    for (int ch = 1; ch <= ChannelCount; ++ch) { m_relay[ch] = false; m_pulsing[ch] = false; }
                    return RelayReply(m_mode, 0, false);
                }

                // "Set input relation" (G0..G3): the board switches how its inputs act on its relays.
                for (int relation = 0; relation <= 3; ++relation)
                {
                    if (sent == m_codec->EncodeSetInputOutputRelation(static_cast<InputOutputRelation>(relation)))
                    {
                        m_inputAction = (relation == 0) ? InputAction::None
                                      : (relation == 1) ? InputAction::Toggle
                                      : (relation == 2) ? InputAction::Latch
                                                        : InputAction::Pulse;
                        if (m_mode == ProtocolMode::Hex)
                            return sent;                              // acknowledged by echoing the command
                        return Text(std::string("OK") + static_cast<char>('0' + relation)); // no terminator
                    }
                }

                for (int ch = 1; ch <= ChannelCount; ++ch)
                {
                    if (sent == m_codec->EncodeReadRelayStatus(ch))
                    {
                        ++m_relayReads;
                        m_pulsing[ch] = false; // measured: reading a pulsing relay cancels its pulse
                        return RelayReply(m_mode, ch, m_relay[ch]);
                    }
                    if (sent == m_codec->EncodeReadInputStatus(ch))
                    {
                        ++m_inputReads;
                        return InputReply(m_mode, ch, m_input[ch]);
                    }
                    if (sent == m_codec->EncodeOpen(ch))
                    {
                        m_relay[ch] = true; m_pulsing[ch] = false;
                        return RelayReply(m_mode, ch, true);
                    }
                    if (sent == m_codec->EncodeClose(ch))
                    {
                        m_relay[ch] = false; m_pulsing[ch] = false;
                        return RelayReply(m_mode, ch, false);
                    }
                    if (sent == m_codec->EncodeToggle(ch))
                    {
                        m_relay[ch] = !m_relay[ch]; m_pulsing[ch] = false;
                        return RelayReply(m_mode, ch, m_relay[ch]);
                    }
                    if (sent == m_codec->EncodeLatch(ch))
                    {
                        // Every other relay closes, silently.
                        for (int other = 1; other <= ChannelCount; ++other)
                        {
                            m_relay[other] = (other == ch);
                            m_pulsing[other] = false;
                        }
                        return RelayReply(m_mode, ch, true);
                    }
                    if (sent == m_codec->EncodeMomentary(ch))
                    {
                        StartPulse(ch, 1000); // measured on the board: 0.8 to 1.0 s
                        return RelayReply(m_mode, ch, true);
                    }
                }

                // AT "AT+D<ch>=<seconds>": Delay (the HEX protocol has none).
                if (m_mode == ProtocolMode::At && sent.size() == 10 && sent[0] == 'A' && sent[3] == 'D')
                {
                    int ch = sent[4] - '0';
                    int seconds = std::atoi(std::string(sent.begin() + 6, sent.end()).c_str());
                    if (ch >= 1 && ch <= ChannelCount)
                    {
                        StartPulse(ch, seconds * 1000);
                        return RelayReply(m_mode, ch, true);
                    }
                }
                return {}; // unknown command: stay silent
            }

        private:
            using Clock = std::chrono::steady_clock;

            void StartPulse(int ch, int durationMs)
            {
                m_relay[ch] = true;
                m_pulsing[ch] = true;
                m_pulseEnd[ch] = Clock::now() + std::chrono::milliseconds(durationMs);
            }

            void ApplyPulseEnds()
            {
                auto now = Clock::now();
                for (int ch = 1; ch <= ChannelCount; ++ch)
                {
                    if (m_pulsing[ch] && now >= m_pulseEnd[ch])
                    {
                        m_relay[ch] = false;
                        m_pulsing[ch] = false;
                    }
                }
            }

            ProtocolMode m_mode;
            InputAction m_inputAction = InputAction::None;
            std::unique_ptr<IProtocolCodec> m_codec;
            std::mutex m_mutex;
            std::array<bool, ChannelCount + 1> m_relay;
            std::array<bool, ChannelCount + 1> m_input;
            std::array<bool, ChannelCount + 1> m_pulsing;
            std::array<Clock::time_point, ChannelCount + 1> m_pulseEnd;
            std::atomic<int> m_relayReads{ 0 };
            std::atomic<int> m_inputReads{ 0 };
        };

        // Thread-safe collector for the events a Controller raises.
        class EventLog
        {
        public:
            void Add(const Event& ev)
            {
                std::lock_guard<std::mutex> l(m_mutex);
                m_events.push_back(ev);
            }

            template <class Pred>
            int Count(Pred pred)
            {
                std::lock_guard<std::mutex> l(m_mutex);
                int n = 0;
                for (const Event& ev : m_events)
                    if (pred(ev)) ++n;
                return n;
            }

            bool HasRelay(int ch, RelayState st)
            {
                return Count([&](const Event& e) {
                    return e.type == EventType::RelayStateChanged && e.channel == ch && e.relayState == st; }) > 0;
            }

            bool HasInput(int ch, InputState st)
            {
                return Count([&](const Event& e) {
                    return e.type == EventType::InputStateChanged && e.channel == ch && e.inputState == st; }) > 0;
            }

            int CountType(EventType type)
            {
                return Count([&](const Event& e) { return e.type == type; });
            }

        private:
            std::mutex m_mutex;
            std::vector<Event> m_events;
        };

        template <class Pred>
        bool WaitFor(Pred pred, int timeoutMs)
        {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (pred())
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return pred();
        }

        void TestControllerIntegration()
        {
            // --- SendOpen writes the correct HEX frame ---------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                OperationResult connectResult = controller.Connect(L"MOCK");
                Check(connectResult.IsOk(), "Controller(HEX): Connect succeeds on mock transport");

                mock->QueueIncomingBytes(HexResponse(3, 0x01)); // the board's answer
                OperationResult sendResult = controller.SendOpen(3);
                Check(sendResult.IsOk(), "Controller(HEX): SendOpen(3) reports success");

                const auto& sent = mock->GetSentHistory();
                bool correctFrame = sent.size() == 1 &&
                    BufferEquals(sent[0], { 0x55,0x56,0x00,0x00,0x00,0x03,0x01,0xAF });
                Check(correctFrame, "Controller(HEX): SendOpen(3) writes the exact expected frame");
            }

            // --- SendOpen writes the correct AT command ---------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                mock->QueueIncomingBytes(RelayReply(ProtocolMode::At, 5, true)); // the board's answer
                controller.SendOpen(5);

                const auto& sent = mock->GetSentHistory();
                bool correctFrame = sent.size() == 1 && BufferEqualsString(sent[0], "AT+O5");
                Check(correctFrame, "Controller(AT): SendOpen(5) writes 'AT+O5' (no terminator)");
            }

            // --- QueryRelayStatus: success path (HEX) ------------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                // Pre-queue the device's answer for "read status channel 2"
                // before issuing the query: the mock has no request/response
                // correlation, it just serves bytes in FIFO order.
                mock->QueueIncomingBytes({ 0x33,0x3C,0x00,0x00,0x00,0x02,0x01,0x72 }); // relay open, ch2

                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(2, state, 200);
                Check(result.IsOk() && state == RelayState::Open,
                      "Controller(HEX): QueryRelayStatus(2) decodes a queued response correctly");
            }

            // --- QueryRelayStatus: timeout path (nothing queued) -------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(1, state, 50); // short timeout on purpose
                Check(result.code == ResultCode::Timeout,
                      "Controller(HEX): QueryRelayStatus times out when the device stays silent");
            }

            // --- QueryInputStatus: success path (AT) --------------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                std::string line = "ON4"; // real board: no terminator at all
                mock->QueueIncomingBytes(ByteBuffer(line.begin(), line.end()));

                InputState state = InputState::Unknown;
                OperationResult result = controller.QueryInputStatus(4, state, 200);
                Check(result.IsOk() && state == InputState::On,
                      "Controller(AT): QueryInputStatus(4) decodes an unterminated 'ON4' response correctly");
            }

            // --- Not connected -> NotConnected, not a crash --------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                Controller controller(ProtocolMode::Hex, std::move(transport));
                // Deliberately not calling Connect().
                OperationResult result = controller.SendOpen(1);
                Check(result.code == ResultCode::NotConnected,
                      "Controller: SendOpen before Connect() reports NotConnected");
            }

            // --- Hardening: several mismatched responses before a real timeout ---
            // Simulates other channels' responses arriving on the wire (as HEX
            // has no request/response correlation) while we're waiting for a
            // specific one that never comes. Query() must discard each of them
            // and still time out cleanly, rather than matching the wrong one
            // or hanging.
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes({ 0x33,0x3C,0x00,0x00,0x00,0x02,0x01,0x72 }); // ch2, not ch5
                mock->QueueIncomingBytes({ 0x33,0x3C,0x00,0x00,0x00,0x03,0x02,0x74 }); // ch3, not ch5
                mock->QueueIncomingBytes({ 0x33,0x3C,0x00,0x00,0x00,0x07,0x01,0x77 }); // ch7, not ch5

                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(5, state, 100);
                Check(result.code == ResultCode::Timeout,
                      "Controller(HEX): mismatched responses are discarded, query still times out cleanly");
            }

            // --- Hardening: a real transport error is reported immediately, ---
            // --- not confused with a plain "no data yet" timeout ---------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");
                mock->SetForcedError(ResultCode::TransportError);

                RelayState state = RelayState::Unknown;
                auto start = std::chrono::steady_clock::now();
                OperationResult result = controller.QueryRelayStatus(1, state, 5000); // generous timeout
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();

                Check(result.code == ResultCode::TransportError,
                      "Controller(HEX): a real transport error is reported as TransportError, not Timeout");
                Check(elapsedMs < 1000,
                      "Controller(HEX): a real transport error returns promptly, without waiting out the full timeout");
            }

            // --- Real AT framing, end to end -------------------------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                std::string reply = "\nClose1\n"; // reply to AT+R1 on a real board
                mock->QueueIncomingBytes(ByteBuffer(reply.begin(), reply.end()));

                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(1, state, 200);
                Check(result.IsOk() && state == RelayState::Closed,
                      "Controller(AT): QueryRelayStatus decodes a real '\\nClose1\\n' reply");
            }
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                std::string reply = "OFF1"; // reply to AT+I1: this is what used to time out
                mock->QueueIncomingBytes(ByteBuffer(reply.begin(), reply.end()));

                InputState state = InputState::Unknown;
                OperationResult result = controller.QueryInputStatus(1, state, 200);
                Check(result.IsOk() && state == InputState::Off,
                      "Controller(AT): QueryInputStatus decodes an unterminated 'OFF1' reply (was timing out)");
            }

            // --- Real HEX framing, end to end --------------------------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes({ 0x33,0x3C,0x00,0x00,0x00,0x01,0x81,0xF1 }); // input 1 grounded

                InputState state = InputState::Unknown;
                OperationResult result = controller.QueryInputStatus(1, state, 200);
                Check(result.IsOk() && state == InputState::On,
                      "Controller(HEX): QueryInputStatus decodes a real 0x81 (grounded) reply");
            }
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes({ 0x33,0x3C,0x00,0x00,0x00,0x01,0x02,0x72 }); // relay 1 closed

                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(1, state, 200);
                Check(result.IsOk() && state == RelayState::Closed,
                      "Controller(HEX): QueryRelayStatus decodes a real read-status reply");
            }
            {
                // A stray byte in front of a valid frame must not make the query fail.
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes({ 0x00, 0x33,0x3C,0x00,0x00,0x00,0x01,0x01,0x71 });

                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(1, state, 200);
                Check(result.IsOk() && state == RelayState::Open,
                      "Controller(HEX): a stray byte before a valid reply is skipped, the reply is still found");
            }

            // =================== Send* waits for the board's answer ===================

            // --- Silent port: reported as a Timeout, after really waiting -----------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                auto start = std::chrono::steady_clock::now();
                OperationResult result = controller.SendOpen(1); // nobody answers
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();

                Check(result.code == ResultCode::Timeout,
                      "Controller: SendOpen with no answer from the board reports Timeout");
                Check(elapsedMs >= 200 && elapsedMs < 1500,
                      "Controller: a silent port blocks Send* for about 250 ms, not forever");
            }

            // --- A confirmed Send* publishes the relay's new state ------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::At, 3, true));
                controller.SendOpen(3);
                Check(log.HasRelay(3, RelayState::Open) && log.CountType(EventType::RelayStateChanged) == 1,
                      "Controller: a confirmed SendOpen raises RelayStateChanged for that relay");

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::At, 3, true));
                controller.SendOpen(3);
                Check(log.CountType(EventType::RelayStateChanged) == 1,
                      "Controller: repeating a command that changes nothing raises no new event");
            }

            // --- Latch also reports the other relays as closed -----------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::Hex, 1, true));
                controller.SendOpen(1);

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::Hex, 2, true)); // the board only mentions relay 2
                controller.SendLatch(2);

                Check(log.HasRelay(2, RelayState::Open) && log.HasRelay(1, RelayState::Closed),
                      "Controller: SendLatch(2) reports relay 2 open AND relay 1 closed (silent on the wire)");
            }

            // --- "All" commands update every relay ----------------------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::At, 0, true)); // "\nOpenA \n"
                OperationResult result = controller.SendAllOpen();

                bool allOpen = true;
                for (int ch = 1; ch <= ChannelCount; ++ch)
                    allOpen = allOpen && log.HasRelay(ch, RelayState::Open);
                Check(result.IsOk() && allOpen,
                      "Controller(AT): SendAllOpen reports all 8 relays open ('OpenA' answer)");
            }

            // --- I/O relation: acknowledged in both protocols -------------------------
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes(Text("OK1")); // no terminator, as on the real board
                Check(controller.SendSetInputOutputRelation(InputOutputRelation::SelfLocking).IsOk(),
                      "Controller(AT): SendSetInputOutputRelation accepts the unterminated 'OK1'");
            }
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes({ 0x55,0x56,0x00,0x00,0x00,0x01,0x0A,0xB6 }); // the command, echoed
                Check(controller.SendSetInputOutputRelation(InputOutputRelation::SelfLocking).IsOk(),
                      "Controller(HEX): SendSetInputOutputRelation accepts the echoed command frame");
            }

            // --- Bytes left over from an earlier session are discarded on Connect -----
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                Controller controller(ProtocolMode::At, std::move(transport));

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::At, 1, true)); // stale "Open1", queued BEFORE Connect
                controller.Connect(L"MOCK");

                mock->QueueIncomingBytes(RelayReply(ProtocolMode::At, 1, false)); // the real, current answer
                RelayState state = RelayState::Unknown;
                OperationResult result = controller.QueryRelayStatus(1, state, 200);
                Check(result.IsOk() && state == RelayState::Closed,
                      "Controller: a stale reply left in the buffer is not mistaken for the current one");
            }

            // =================== Polling against a simulated board ===================

            // --- AT: initial sync, then input change, silent relay change, concurrent Send ---
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);

                bool synced = WaitFor([&]() {
                    return log.CountType(EventType::RelayStateChanged) == ChannelCount &&
                           log.CountType(EventType::InputStateChanged) == ChannelCount; }, 4000);
                Check(synced, "Polling(AT): the first round reports all 8 relays and all 8 inputs");

                bool allClosedAndOff = true;
                for (int ch = 1; ch <= ChannelCount; ++ch)
                    allClosedAndOff = allClosedAndOff && log.HasRelay(ch, RelayState::Closed) && log.HasInput(ch, InputState::Off);
                Check(allClosedAndOff, "Polling(AT): initial states are read correctly (relays closed, inputs off)");

                board.SetInput(3, true); // someone grounds input 3
                Check(WaitFor([&]() { return log.HasInput(3, InputState::On); }, 3000),
                      "Polling(AT): a physical input change is detected");

                board.SetRelay(2, true); // a relay changes on its own, e.g. Momentary falling back
                Check(WaitFor([&]() { return log.HasRelay(2, RelayState::Open); }, 3000),
                      "Polling(AT): a relay that changes on its own is detected");

                OperationResult sendResult = controller.SendOpen(6); // a click while polling runs
                Check(sendResult.IsOk() && board.GetRelay(6) && log.HasRelay(6, RelayState::Open),
                      "Polling(AT): a Send* issued while polling runs works, and its result is published");

                controller.StopPolling();
            }

            // --- HEX: initial sync ------------------------------------------------------
            {
                FakeBoard board(ProtocolMode::Hex);
                board.SetRelay(4, true);
                board.SetInput(7, true);

                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);

                bool synced = WaitFor([&]() {
                    return log.CountType(EventType::RelayStateChanged) == ChannelCount &&
                           log.CountType(EventType::InputStateChanged) == ChannelCount; }, 4000);
                Check(synced && log.HasRelay(4, RelayState::Open) && log.HasRelay(1, RelayState::Closed) &&
                      log.HasInput(7, InputState::On) && log.HasInput(1, InputState::Off),
                      "Polling(HEX): the first round reports every relay and input with the right state");

                controller.StopPolling();
            }

            // =================== Momentary / Delay must survive the polling ===================

            // --- The simulated board really models the hazard (otherwise the next tests prove nothing) ---
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                controller.SendMomentary(1);
                RelayState state = RelayState::Unknown;
                controller.QueryRelayStatus(1, state, 200); // a direct read of the pulsing relay, as measured on the board
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));

                Check(board.GetRelay(1),
                      "FakeBoard: reading a pulsing relay cancels the pulse (models the real board)");
            }

            // --- Momentary falls back although polling is running ----------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                controller.SendMomentary(1);
                board.SetInput(3, true); // another channel's input changes during the pulse

                Check(WaitFor([&]() { return log.HasInput(3, InputState::On); }, 600),
                      "Polling(AT): everything else keeps being polled while a relay is pulsing");

                Check(WaitFor([&]() { return !board.GetRelay(1); }, 3000),
                      "Polling(AT): a Momentary pulse falls back while polling runs (the pulsing relay is not read)");

                auto relay1Closed = [](const Event& e) {
                    return e.type == EventType::RelayStateChanged && e.channel == 1 && e.relayState == RelayState::Closed; };
                Check(WaitFor([&]() { return log.Count(relay1Closed) >= 2; }, 4000),
                      "Polling(AT): once the pulse is over, the relay is read again and reported closed");

                controller.StopPolling();
            }

            // --- Delay too ----------------------------------------------------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                controller.SendDelay(2, 1); // 1 second

                Check(WaitFor([&]() { return !board.GetRelay(2); }, 4000),
                      "Polling(AT): a Delay pulse falls back while polling runs");

                auto relay2Closed = [](const Event& e) {
                    return e.type == EventType::RelayStateChanged && e.channel == 2 && e.relayState == RelayState::Closed; };
                Check(WaitFor([&]() { return log.Count(relay2Closed) >= 2; }, 5000),
                      "Polling(AT): after a Delay pulse, the relay is reported closed");

                controller.StopPolling();
            }

            // --- Closing a pulsing relay ends the protection at once --------------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                controller.SendMomentary(1);
                controller.SendClose(1);   // nothing left to protect
                board.SetRelay(1, true);   // a change from outside, silent

                auto relay1Open = [](const Event& e) {
                    return e.type == EventType::RelayStateChanged && e.channel == 1 && e.relayState == RelayState::Open; };
                // One Open event came from SendMomentary; a second one can only come from the
                // polling reading relay 1 again - well before the 1 s protection would have ended.
                Check(WaitFor([&]() { return log.Count(relay1Open) >= 2; }, 700),
                      "Polling(AT): closing a pulsing relay lifts the protection immediately");

                controller.StopPolling();
            }

            // =================== Pulses started by the board itself (input relations) ===================

            // --- G3-like: an input edge starts a pulse; the polling must not cancel it -------------------
            {
                FakeBoard board(ProtocolMode::At);
                board.SetInputAction(FakeBoard::InputAction::Pulse);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                board.SetInput(1, true); // grounded and HELD, as in the bench test
                Check(WaitFor([&]() { return !board.GetRelay(1) && log.HasInput(1, InputState::On); }, 3000) &&
                      !board.GetRelay(1),
                      "Polling(AT): a pulse started by an input edge falls back (the relay is read after the input, not during the pulse)");

                controller.StopPolling();
            }

            // --- G1-like: a relay toggled by its input is still reported ------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                board.SetInputAction(FakeBoard::InputAction::Toggle);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                board.SetInput(2, true);
                Check(WaitFor([&]() { return log.HasRelay(2, RelayState::Open); }, 4000),
                      "Polling(AT): a relay switched by its input is reported (after the protective delay)");

                controller.StopPolling();
            }

            // --- An input edge must not cut a longer protection short ------------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                controller.SendDelay(1, 2); // pulse of 2 s: protected for about 3 s
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                board.SetInput(1, true);    // an input edge: its 1 s protection must not replace the longer one

                Check(WaitFor([&]() { return !board.GetRelay(1); }, 4500),
                      "Polling(AT): an input edge does not shorten the protection of a running Delay");

                controller.StopPolling();
            }

            // --- An input that is already on when polling starts is not an "edge" ----------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                board.SetInput(3, true);
                board.SetRelay(3, true);

                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);

                // Well under the 1 s protection: the first round must already know relay 3.
                Check(WaitFor([&]() { return log.HasRelay(3, RelayState::Open) &&
                                             log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 600),
                      "Polling(AT): the first observation of an input is not treated as an edge (no initial delay)");

                controller.StopPolling();
            }

            // =================== Polling that knows the input relation ===================

            // --- G1 known: no protective delay, a relay switched by its input shows up at once ------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                controller.SendSetInputOutputRelation(InputOutputRelation::SelfLocking); // the board AND the library now know G1

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::RelayStateChanged) == ChannelCount; }, 4000);

                board.SetInput(2, true); // toggles relay 2
                Check(WaitFor([&]() { return log.HasRelay(2, RelayState::Open); }, 600),
                      "Polling(G1 known): a relay switched by its input is reported at once (no 2 s protective delay)");

                controller.StopPolling();
            }

            // --- G3 known: relays are never read, so even a tap the polling cannot see leaves the pulse alone ---
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                controller.SendSetInputOutputRelation(InputOutputRelation::Momentary); // G3

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::InputStateChanged) == ChannelCount; }, 4000);

                // A tap shorter than one polling round: the polling never sees the input on.
                board.SetInput(1, true);
                board.SetInput(1, false);

                Check(WaitFor([&]() { return !board.GetRelay(1); }, 4000),
                      "Polling(G3 known): a pulse started by a tap the polling never saw falls back");
                Check(board.RelayReads() == 0 && board.InputReads() > 0,
                      "Polling(G3 known): relays are never read, inputs keep being read");
                Check(log.CountType(EventType::RelayStateChanged) == 0,
                      "Polling(G3 known): no relay state is reported by the polling");

                controller.StopPolling();
            }

            // --- G3 known: commands still report their relay ---------------------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                controller.SendSetInputOutputRelation(InputOutputRelation::Momentary);

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);

                controller.SendOpen(3);
                Check(log.HasRelay(3, RelayState::Open),
                      "Polling(G3 known): a Send* still publishes its relay's state");

                controller.StopPolling();
            }

            // --- Telling the library, without sending anything, has the same effect -----------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                controller.SetKnownInputOutputRelation(InputOutputRelation::Momentary); // nothing is sent

                controller.StartPolling(20);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                controller.StopPolling();

                Check(board.RelayReads() == 0 && board.InputReads() > 0,
                      "Polling: SetKnownInputOutputRelation(G3) stops relay reads without sending anything");
            }

            // --- Leaving G3 resumes the relay reads --------------------------------------------------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                controller.SendSetInputOutputRelation(InputOutputRelation::Momentary);

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);
                WaitFor([&]() { return log.CountType(EventType::InputStateChanged) == ChannelCount; }, 4000);

                board.SetRelay(2, true); // changes silently; in G3 nobody looks
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                bool unseenInG3 = !log.HasRelay(2, RelayState::Open);

                controller.SendSetInputOutputRelation(InputOutputRelation::SelfLocking); // back to a safe mode
                Check(unseenInG3 && WaitFor([&]() { return log.HasRelay(2, RelayState::Open); }, 1000),
                      "Polling: switching from G3 to G1 makes the relays be read (and reported) again");

                controller.StopPolling();
            }

            // --- The relation is forgotten on Connect (the board's mode cannot be read back) -----------------------
            {
                FakeBoard board(ProtocolMode::At);
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();
                mock->SetResponder([&](const ByteBuffer& sent) { return board.Respond(sent); });

                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");
                controller.SetKnownInputOutputRelation(InputOutputRelation::Momentary);
                controller.Disconnect();
                controller.Connect(L"MOCK"); // a new session: the library no longer assumes G3

                controller.StartPolling(20);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                controller.StopPolling();

                Check(board.RelayReads() > 0,
                      "Controller: the known relation is forgotten on Connect, relays are read again");
            }

            // --- A port that is open but never answers: ONE error, then polling stops -------
            {
                auto transport = std::make_unique<MockSerialTransport>(); // no responder: silent
                Controller controller(ProtocolMode::At, std::move(transport));
                controller.Connect(L"MOCK");

                EventLog log;
                controller.SetEventCallback([&](const Event& ev) { log.Add(ev); });
                controller.StartPolling(20);

                bool raised = WaitFor([&]() { return log.CountType(EventType::TransportError) > 0; }, 6000);
                std::this_thread::sleep_for(std::chrono::milliseconds(300)); // would a second one follow?

                bool timeoutCode = log.Count([](const Event& e) {
                    return e.type == EventType::TransportError && e.errorCode == ResultCode::Timeout; }) == 1;
                Check(raised && timeoutCode && log.CountType(EventType::TransportError) == 1,
                      "Polling: a silent device raises exactly one TransportError(Timeout), then polling stops");

                controller.StopPolling();
            }

            // --- Hardening: input polling survives (and reports) a persistent ---
            // --- transport failure, and StopPolling() still returns promptly --
            {
                auto transport = std::make_unique<MockSerialTransport>();
                MockSerialTransport* mock = transport.get();

                Controller controller(ProtocolMode::Hex, std::move(transport));
                controller.Connect(L"MOCK");
                mock->SetForcedError(ResultCode::TransportError);

                std::atomic<int> errorEventCount{ 0 };
                std::mutex callbackMutex;
                controller.SetEventCallback([&](const Event& ev)
                    {
                        std::lock_guard<std::mutex> lock(callbackMutex);
                        if (ev.type == EventType::TransportError)
                            ++errorEventCount;
                    });

                controller.StartPolling(20); // fast interval, this is a test
                std::this_thread::sleep_for(std::chrono::milliseconds(150));

                auto stopStart = std::chrono::steady_clock::now();
                controller.StopPolling(); // must return (join) promptly, not hang
                auto stopElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - stopStart).count();

                Check(errorEventCount.load() == 1,
                      "Controller: a lost link raises exactly ONE TransportError event, then polling stops by itself");
                Check(stopElapsedMs < 1000,
                      "Controller: StopPolling() returns promptly even while the transport keeps failing");
            }
        }
    } // namespace

    bool RunSelfTests()
    {
        g_testCount = 0;
        g_failCount = 0;

        std::cout << "=== UD85C08 self-tests (no hardware required) ===\n\n";

        std::cout << "-- HEX codec: encoding --\n";
        TestHexEncoding();
        std::cout << "\n-- HEX codec: decoding --\n";
        TestHexDecoding();
        std::cout << "\n-- AT codec: encoding --\n";
        TestAtEncoding();
        std::cout << "\n-- AT codec: decoding --\n";
        TestAtDecoding();
        std::cout << "\n-- Port name normalization --\n";
        TestPortName();
        std::cout << "\n-- Controller + MockSerialTransport integration --\n";
        TestControllerIntegration();

        std::cout << "\n" << (g_testCount - g_failCount) << "/" << g_testCount << " tests passed.\n";
        return g_failCount == 0;
    }

} // namespace UD85C08
