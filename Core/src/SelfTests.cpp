// SelfTests.cpp
// Implements UD85C08::RunSelfTests(). Internal file, but the function it
// implements is declared in the public UD85C08_SelfTest.h so that CLI/GUI
// can call it without needing access to the internal codec headers.

#include "../include/UD85C08_SelfTest.h"
#include "../include/UD85C08_Controller.h"
#include "../include/UD85C08_MockSerialTransport.h"
#include "AtCommandCodec.h"
#include "HexFrameCodec.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <tuple>
#include <atomic>
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
        }

        void TestAtEncoding()
        {
            AtCommandCodec codec;

            Check(BufferEqualsString(codec.EncodeOpen(3), "AT+O3\r\n"), "AT: open channel 3");
            Check(BufferEqualsString(codec.EncodeClose(8), "AT+C8\r\n"), "AT: close channel 8");
            Check(BufferEqualsString(codec.EncodeToggle(1), "AT+T1\r\n"), "AT: toggle channel 1");
            Check(BufferEqualsString(codec.EncodeLatch(2), "AT+L2\r\n"), "AT: latch channel 2");
            Check(BufferEqualsString(codec.EncodeMomentary(5), "AT+M5\r\n"), "AT: momentary channel 5");
            Check(BufferEqualsString(codec.EncodeAllOpen(), "AT+AO\r\n"), "AT: all relays open");
            Check(BufferEqualsString(codec.EncodeAllClose(), "AT+AC\r\n"), "AT: all relays close");
            Check(BufferEqualsString(codec.EncodeReadInputStatus(4), "AT+I4\r\n"), "AT: read input channel 4");
            Check(BufferEqualsString(codec.EncodeSetInputOutputRelation(InputOutputRelation::Interlocking), "AT+G2\r\n"),
                  "AT: set I/O relation G2");

            // Datasheet examples: "AT+D1=0010" and "AT+D2=0100"
            Check(BufferEqualsString(codec.EncodeDelay(1, 10), "AT+D1=0010\r\n"), "AT: delay channel 1, 10s");
            Check(BufferEqualsString(codec.EncodeDelay(2, 100), "AT+D2=0100\r\n"), "AT: delay channel 2, 100s");

            Check(codec.SupportsDelay(), "AT: SupportsDelay() is true");
            Check(codec.EncodeDelay(1, 10000).empty(), "AT: delay out of range (10000) yields empty buffer");
            Check(codec.EncodeOpen(9).empty(), "AT: invalid channel (9) yields empty buffer");
        }

        void TestAtDecoding()
        {
            AtCommandCodec codec;

            auto decodeLine = [&](const std::string& line) {
                ByteBuffer buf(line.begin(), line.end());
                DecodedResponse resp;
                size_t consumed = 0;
                bool decoded = codec.TryDecode(buf, resp, consumed);
                return std::make_tuple(decoded, consumed, resp);
            };

            {
                auto [decoded, consumed, resp] = decodeLine("Open1\r\n");
                Check(decoded && consumed == 7 && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 1 && resp.relayState == RelayState::Open,
                      "AT: decode 'Open1'");
            }
            {
                auto [decoded, consumed, resp] = decodeLine("Close8\r\n");
                Check(decoded && resp.kind == ResponseKind::RelayStatus &&
                      resp.channel == 8 && resp.relayState == RelayState::Closed,
                      "AT: decode 'Close8'");
            }
            {
                auto [decoded, consumed, resp] = decodeLine("ON2\r\n");
                Check(decoded && resp.kind == ResponseKind::InputStatus &&
                      resp.channel == 2 && resp.inputState == InputState::On,
                      "AT: decode 'ON2'");
            }
            {
                auto [decoded, consumed, resp] = decodeLine("OFF5\r\n");
                Check(decoded && resp.kind == ResponseKind::InputStatus &&
                      resp.channel == 5 && resp.inputState == InputState::Off,
                      "AT: decode 'OFF5'");
            }
            {
                auto [decoded, consumed, resp] = decodeLine("OK2\r\n");
                Check(decoded && resp.kind == ResponseKind::Acknowledge && resp.channel == 2,
                      "AT: decode 'OK2'");
            }
            {
                // Incomplete line (no terminator yet) -> needs more data
                ByteBuffer buf{ 'O','p','e','n','1' };
                DecodedResponse resp;
                size_t consumed = 0;
                bool decoded = codec.TryDecode(buf, resp, consumed);
                Check(!decoded && consumed == 0, "AT: incomplete line reports 'need more data'");
            }
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
                controller.SendOpen(5);

                const auto& sent = mock->GetSentHistory();
                bool correctFrame = sent.size() == 1 && BufferEqualsString(sent[0], "AT+O5\r\n");
                Check(correctFrame, "Controller(AT): SendOpen(5) writes 'AT+O5\\r\\n'");
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

                std::string line = "ON4\r\n";
                mock->QueueIncomingBytes(ByteBuffer(line.begin(), line.end()));

                InputState state = InputState::Unknown;
                OperationResult result = controller.QueryInputStatus(4, state, 200);
                Check(result.IsOk() && state == InputState::On,
                      "Controller(AT): QueryInputStatus(4) decodes a queued 'ON4' response correctly");
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

            // --- Hardening: input polling survives (and reports) a persistent ---
            // --- transport failure, and StopInputPolling() still returns promptly --
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

                controller.StartInputPolling(20); // fast interval, this is a test
                std::this_thread::sleep_for(std::chrono::milliseconds(150));

                auto stopStart = std::chrono::steady_clock::now();
                controller.StopInputPolling(); // must return (join) promptly, not hang
                auto stopElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - stopStart).count();

                Check(errorEventCount.load() > 0,
                      "Controller: polling under a persistent transport error raises TransportError events");
                Check(stopElapsedMs < 1000,
                      "Controller: StopInputPolling() returns promptly even while the transport keeps failing");
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
        std::cout << "\n-- Controller + MockSerialTransport integration --\n";
        TestControllerIntegration();

        std::cout << "\n" << (g_testCount - g_failCount) << "/" << g_testCount << " tests passed.\n";
        return g_failCount == 0;
    }

} // namespace UD85C08
