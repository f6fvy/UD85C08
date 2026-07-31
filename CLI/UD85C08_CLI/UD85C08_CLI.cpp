// UD85C08_CLI.cpp - entry point
//
// A small command-line tool to drive a UD85C08 board, and to run the
// library's self-tests (which need no hardware at all).
//
// NOTE: the --port / --open / --close / ... commands below have been
// written against the public UD85C08::Controller API and compile
// cleanly, but have not yet been exercised against a physical board
// (none was available while writing this). Expect to iron out a few
// details (exact response timing, port name format, ...) once real
// hardware is on the desk - see the comments in SerialTransport.cpp /
// AtCommandCodec.cpp / HexFrameCodec.h for the specific open points.

#include "UD85C08_SelfTest.h"
#include "UD85C08_Controller.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace UD85C08;

namespace
{
    void PrintUsage()
    {
        std::cout <<
            "UD85C08_CLI - command-line control of the UD85C08 8DI-8DO USB relay board\n"
            "\n"
            "  UD85C08_CLI --selftest\n"
            "      Runs the codec/controller self-tests. No hardware required.\n"
            "\n"
            "  UD85C08_CLI --port <COMx> [--protocol at|hex] <command> [args...]\n"
            "\n"
            "  Commands (one per run):\n"
            "    --open <channel>              Open relay (1-8)\n"
            "    --close <channel>              Close relay (1-8)\n"
            "    --toggle <channel>             Toggle relay (1-8)\n"
            "    --latch <channel>              Latch/interlock relay (1-8)\n"
            "    --momentary <channel>          Momentary pulse on relay (1-8)\n"
            "    --delay <channel> <seconds>    Open, then auto-close after N seconds (AT only)\n"
            "    --all-open                     Open all relays\n"
            "    --all-close                    Close all relays\n"
            "    --set-relation <0-3>            0=Unrelated 1=SelfLocking 2=Interlocking 3=Momentary\n"
            "    --read-relay <channel>          Query and print a relay's state\n"
            "    --read-input <channel>          Query and print an input's state\n"
            "    --monitor-inputs [--interval ms] Continuously poll all 8 inputs, print changes\n"
            "                                     (press Enter to stop)\n"
            "\n"
            "  --protocol defaults to 'hex' if not specified.\n";
    }

    std::wstring ToWide(const std::string& s)
    {
        return std::wstring(s.begin(), s.end()); // port names are plain ASCII (e.g. "COM5")
    }

    // Human-readable name for a ResultCode. Uses the shared implementation
    // in UD85C08_Types.h rather than duplicating this switch here.
    const char* ResultCodeToString(ResultCode code)
    {
        return UD85C08::ToString(code);
    }

    void PrintResult(const char* label, const OperationResult& result)
    {
        if (result.IsOk())
            std::cout << label << ": OK\n";
        else
            std::cout << label << ": FAILED (" << ResultCodeToString(result.code) << ")\n";
    }

    int RunCommand(const std::vector<std::string>& args)
    {
        std::string portName;
        ProtocolMode protocol = ProtocolMode::Hex;

        size_t i = 0;
        auto next = [&]() -> std::string {
            if (i + 1 >= args.size())
            {
                std::cerr << "Missing argument after '" << args[i] << "'\n";
                std::exit(1);
            }
            return args[++i];
            };

        // First pass: pull out --port and --protocol, wherever they appear.
        std::vector<std::string> remaining;
        for (i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--port")
                portName = next();
            else if (args[i] == "--protocol")
            {
                std::string p = next();
                if (p == "at") protocol = ProtocolMode::At;
                else if (p == "hex") protocol = ProtocolMode::Hex;
                else { std::cerr << "Unknown protocol: " << p << " (expected 'at' or 'hex')\n"; return 1; }
            }
            else
                remaining.push_back(args[i]);
        }

        if (portName.empty())
        {
            std::cerr << "Missing --port <COMx>\n";
            PrintUsage();
            return 1;
        }

        if (remaining.empty())
        {
            std::cerr << "Missing command (--open, --close, ...)\n";
            PrintUsage();
            return 1;
        }

        Controller controller(protocol);
        OperationResult connectResult = controller.Connect(ToWide(portName));
        if (!connectResult.IsOk())
        {
            std::cerr << "Could not open " << portName << " (" << ResultCodeToString(connectResult.code) << ")\n";
            return 1;
        }

        const std::string& cmd = remaining[0];

        if (cmd == "--open" || cmd == "--close" || cmd == "--toggle" ||
            cmd == "--latch" || cmd == "--momentary")
        {
            if (remaining.size() < 2) { std::cerr << "Missing channel number\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());

            OperationResult result;
            if (cmd == "--open")           result = controller.SendOpen(channel);
            else if (cmd == "--close")     result = controller.SendClose(channel);
            else if (cmd == "--toggle")    result = controller.SendToggle(channel);
            else if (cmd == "--latch")     result = controller.SendLatch(channel);
            else /* momentary */           result = controller.SendMomentary(channel);

            PrintResult(cmd.c_str() + 2, result);
            return result.IsOk() ? 0 : 1;
        }
        else if (cmd == "--delay")
        {
            if (remaining.size() < 3) { std::cerr << "Usage: --delay <channel> <seconds>\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());
            int seconds = std::atoi(remaining[2].c_str());

            if (!controller.SupportsDelay())
            {
                std::cerr << "The Delay command is only available with --protocol at\n";
                return 1;
            }
            OperationResult result = controller.SendDelay(channel, seconds);
            PrintResult("delay", result);
            return result.IsOk() ? 0 : 1;
        }
        else if (cmd == "--all-open")
        {
            OperationResult result = controller.SendAllOpen();
            PrintResult("all-open", result);
            return result.IsOk() ? 0 : 1;
        }
        else if (cmd == "--all-close")
        {
            OperationResult result = controller.SendAllClose();
            PrintResult("all-close", result);
            return result.IsOk() ? 0 : 1;
        }
        else if (cmd == "--set-relation")
        {
            if (remaining.size() < 2) { std::cerr << "Usage: --set-relation <0-3>\n"; return 1; }
            int value = std::atoi(remaining[1].c_str());
            if (value < 0 || value > 3) { std::cerr << "Value must be 0-3\n"; return 1; }

            OperationResult result = controller.SendSetInputOutputRelation(static_cast<InputOutputRelation>(value));
            PrintResult("set-relation", result);
            return result.IsOk() ? 0 : 1;
        }
        else if (cmd == "--read-relay")
        {
            if (remaining.size() < 2) { std::cerr << "Usage: --read-relay <channel>\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());

            RelayState state = RelayState::Unknown;
            OperationResult result = controller.QueryRelayStatus(channel, state);
            if (!result.IsOk())
            {
                PrintResult("read-relay", result);
                return 1;
            }
            std::cout << "Relay " << channel << ": "
                << (state == RelayState::Open ? "Open" : state == RelayState::Closed ? "Closed" : "Unknown")
                << "\n";
            return 0;
        }
        else if (cmd == "--read-input")
        {
            if (remaining.size() < 2) { std::cerr << "Usage: --read-input <channel>\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());

            InputState state = InputState::Unknown;
            OperationResult result = controller.QueryInputStatus(channel, state);
            if (!result.IsOk())
            {
                PrintResult("read-input", result);
                return 1;
            }
            std::cout << "Input " << channel << ": "
                << (state == InputState::On ? "On" : state == InputState::Off ? "Off" : "Unknown")
                << "\n";
            return 0;
        }
        else if (cmd == "--monitor-inputs")
        {
            int intervalMs = 200;
            for (size_t j = 1; j < remaining.size(); ++j)
            {
                if (remaining[j] == "--interval" && j + 1 < remaining.size())
                    intervalMs = std::atoi(remaining[++j].c_str());
            }

            controller.SetEventCallback([](const Event& ev) {
                if (ev.type == EventType::InputStateChanged)
                {
                    std::cout << "Input " << ev.channel << " -> "
                        << (ev.inputState == InputState::On ? "On" : "Off") << "\n";
                }
                else if (ev.type == EventType::TransportError)
                {
                    std::cout << "Transport error on channel " << ev.channel
                        << " (" << ResultCodeToString(ev.errorCode) << ")\n";
                }
                });

            std::cout << "Monitoring inputs every " << intervalMs << " ms. Press Enter to stop.\n";
            controller.StartInputPolling(intervalMs);
            std::cin.get();
            controller.StopInputPolling();
            return 0;
        }
        else
        {
            std::cerr << "Unknown command: " << cmd << "\n";
            PrintUsage();
            return 1;
        }
    }
} // namespace

int main(int argc, char* argv[])
{
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "--help" || args[0] == "-h")
    {
        PrintUsage();
        return 0;
    }

    if (args[0] == "--selftest")
    {
        bool allPassed = UD85C08::RunSelfTests();
        return allPassed ? 0 : 1;
    }

    return RunCommand(args);
}
