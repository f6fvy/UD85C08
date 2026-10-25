// UD85C08_CLI.cpp - entry point
//
// A small command-line tool to drive a UD85C08 board, and to run the
// library's self-tests (which need no hardware at all).
//
// The hardware commands (--open, --close, --read-relay, --monitor-inputs, ...)
// and the diagnostics (--probe-pulse, --measure-pulse, --probe-input-pulse)
// are validated on a real board, in AT and in HEX. What the board really does
// is documented in DEVICE_BEHAVIOR.md.

#include "UD85C08_SelfTest.h"
#include "UD85C08_Controller.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iomanip>
#include <thread>

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
            "    --probe-pulse <channel> [delay <s>]  Diagnostic: does traffic on the link disturb the\n"
            "                                     board's own Momentary (or Delay) timer? Plays several\n"
            "                                     scenarios and checks whether the relay falls back\n"
            "                                     (about 30 s for Momentary)\n"
            "    --measure-pulse <channel> [delay_ms ...]  Diagnostic: measures how long a Momentary\n"
            "                                     command pulse lasts (default 6 delays, no action needed)\n"
            "    --stress-test [rounds]          Diagnostic: reads all 8 relays and 8 inputs back to back,\n"
            "                                     as fast as possible (default 50 rounds of 16 queries),\n"
            "                                     counts lost/failed answers and measures the time per\n"
            "                                     query and per round. Keep the board idle meanwhile\n"
            "    --probe-input-pulse <channel> [delay_ms ...]  Diagnostic for input relation G3 (run\n"
            "                                     --set-relation 3 first): measures how long the relay\n"
            "                                     pulse started by an input lasts. You ground the input\n"
            "                                     when asked, once per delay (default 6 presses)\n"
            "    --monitor-inputs [--interval ms] [--relation 0-3] [--relays LIST] [--inputs LIST]\n"
            "                                     Continuously poll relays and inputs, print changes.\n"
            "                                     --relays / --inputs restrict what is read (shorter\n"
            "                                     rounds): LIST is 'all' (default), 'none' or channels\n"
            "                                     like 1,2,5-7. --relation tells the program which input\n"
            "                                     relation the board is in (nothing is sent). Strongly\n"
            "                                     advised: in G3 (3) relays must not be read, or the pulses\n"
            "                                     the board starts on its own get cancelled\n"
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

    // Parses a non-empty string of decimal digits (at most 4) into an int.
    bool ParseSmallInt(const std::string& text, int& value)
    {
        if (text.empty() || text.size() > 4)
            return false;
        int v = 0;
        for (char c : text)
        {
            if (c < '0' || c > '9')
                return false;
            v = v * 10 + (c - '0');
        }
        value = v;
        return true;
    }

    // Parses a channel list into a mask: "all", "none", or items like "1,2,5-7"
    // (channels 1-8). Returns false on any malformed item.
    bool ParseChannelList(const std::string& text, ChannelMask& mask)
    {
        if (text == "all")  { mask = AllChannels; return true; }
        if (text == "none") { mask = NoChannels;  return true; }

        ChannelMask result = NoChannels;
        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t comma = text.find(',', pos);
            if (comma == std::string::npos)
                comma = text.size();
            const std::string item = text.substr(pos, comma - pos);
            pos = comma + 1;

            int first = 0, last = 0;
            const size_t dash = item.find('-');
            if (dash == std::string::npos)
            {
                if (!ParseSmallInt(item, first)) return false;
                last = first;
            }
            else if (!ParseSmallInt(item.substr(0, dash), first) ||
                     !ParseSmallInt(item.substr(dash + 1), last))
            {
                return false;
            }

            if (first < 1 || last > ChannelCount || first > last)
                return false;
            for (int ch = first; ch <= last; ++ch)
                result = static_cast<ChannelMask>(result | ChannelBit(ch));

            if (comma >= text.size())
                break;
        }
        mask = result;
        return true;
    }

    void SleepMs(int ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    // Diagnostic. The relay's own timer (Momentary ~0.5 s, or Delay) is run by the
    // board's firmware, and it was observed NOT to fall back while the polling
    // thread keeps talking to the board. This replays, in one session and with
    // exact timing, a pulse followed by different kinds of traffic, then checks
    // by itself whether the relay fell back. It isolates which requests are
    // responsible. (See DEVICE_BEHAVIOR.md.)
    int RunPulseProbe(Controller& controller, int channel, bool useDelay, int delaySeconds)
    {
        const int other = (channel == 1) ? 2 : 1;
        const int pulseMs = useDelay ? delaySeconds * 1000 : 500; // nominal duration
        const int settleMs = pulseMs + 2500;                        // wait before checking

        auto readRelay = [&](int ch) { RelayState st = RelayState::Unknown; controller.QueryRelayStatus(ch, st); };
        auto readInput = [&](int ch) { InputState st = InputState::Unknown; controller.QueryInputStatus(ch, st); };

        struct Scenario
        {
            const char* label;
            std::function<void()> traffic;   // sent 100 ms after the pulse starts
            std::function<void()> before = nullptr; // optional: run just before the pulse starts
            std::function<void()> after = nullptr;  // optional: run once the check is done
        };

        const std::vector<Scenario> scenarios =
        {
            { "no traffic at all (control)",                          [&]() {} },
            { "ONE input read, same channel",                          [&]() { readInput(channel); } },
            { "ONE input read, another channel",                       [&]() { readInput(other); } },
            { "ONE relay-status read, same channel",                   [&]() { readRelay(channel); } },
            { "ONE relay-status read, another channel",                [&]() { readRelay(other); } },
            { "burst of 16 reads (all relays + inputs, like polling)", [&]()
                {
                    for (int ch = 1; ch <= ChannelCount; ++ch) { readRelay(ch); readInput(ch); }
                } },
            // The library's own polling thread, running throughout. It is expected
            // to leave the pulsing relay alone, so this one should fall back.
            { "the library's own polling running (with its safeguard)", []() {},
              [&]() { controller.StartPolling(50); SleepMs(300); },
              [&]() { controller.StopPolling(); } },
        };

        std::cout << "Pulse probe on channel " << channel << ": "
                  << (useDelay ? "Delay " + std::to_string(delaySeconds) + " s" : std::string("Momentary"))
                  << ". For each scenario: close the relay, start the pulse, send the traffic 100 ms\n"
                     "later, wait " << settleMs / 1000.0 << " s, then check whether the relay fell back by itself.\n"
                     "Do not touch the board meanwhile.\n\n";

        std::vector<bool> fell(scenarios.size(), false);

        for (size_t i = 0; i < scenarios.size(); ++i)
        {
            // Start every scenario from a known state: relay closed.
            controller.SendClose(channel);
            SleepMs(500);
            RelayState state = RelayState::Unknown;
            if (!controller.QueryRelayStatus(channel, state).IsOk() || state != RelayState::Closed)
            {
                std::cout << "Could not put the relay in the closed state - aborting.\n";
                return 1;
            }

            if (scenarios[i].before)
                scenarios[i].before();

            OperationResult started = useDelay ? controller.SendDelay(channel, delaySeconds)
                                               : controller.SendMomentary(channel);
            if (!started.IsOk())
            {
                std::cout << "Could not start the pulse (" << ResultCodeToString(started.code) << ") - aborting.\n";
                return 1;
            }

            SleepMs(100);
            scenarios[i].traffic();
            SleepMs(settleMs);

            OperationResult check = controller.QueryRelayStatus(channel, state);
            fell[i] = check.IsOk() && state == RelayState::Closed;
            if (scenarios[i].after)
                scenarios[i].after();

            std::cout << "[" << i + 1 << "/" << scenarios.size() << "] " << scenarios[i].label << " -> ";
            if (!check.IsOk())
                std::cout << "no answer when checking (" << ResultCodeToString(check.code) << ")\n";
            else
                std::cout << (fell[i] ? "relay FELL BACK" : "relay STILL OPEN (the pulse was cancelled)") << "\n";
        }

        controller.SendClose(channel); // leave the relay closed

        std::cout << "\nSummary: " << std::count(fell.begin(), fell.end(), true) << " of "
                  << scenarios.size() << " scenarios let the relay fall back.\n";
        return 0;
    }

    // Diagnostic: hammers the board with the same requests the polling uses, back to back,
    // to check that no answer is lost or confused, and to measure how long a query and a full
    // round take (which is what bounds the display latency of the polling).
    int RunStressTest(Controller& controller, int rounds)
    {
        using Clock = std::chrono::steady_clock;

        std::vector<double> queryMs;   // duration of every successful query
        std::vector<double> roundMs;   // duration of every round of 16 queries
        int answered = 0, timeouts = 0, otherErrors = 0, changes = 0;

        std::array<RelayState, ChannelCount + 1> firstRelay{};
        std::array<InputState, ChannelCount + 1> firstInput{};

        std::cout << "Stress test: " << rounds << " rounds x " << 2 * ChannelCount << " queries = "
                  << rounds * 2 * ChannelCount << " queries. Keep the board idle (no pulse running, "
                  << "do not touch the inputs).\n\n";

        auto note = [&](const OperationResult& r, double ms)
        {
            if (r.IsOk())
            {
                ++answered;
                queryMs.push_back(ms);
            }
            else if (r.code == ResultCode::Timeout)
                ++timeouts;
            else
                ++otherErrors;
        };

        const auto wallStart = Clock::now();
        for (int round = 0; round < rounds; ++round)
        {
            const auto roundStart = Clock::now();

            for (Channel ch = 1; ch <= ChannelCount; ++ch)
            {
                RelayState relay = RelayState::Unknown;
                auto t0 = Clock::now();
                OperationResult rr = controller.QueryRelayStatus(ch, relay, 500);
                note(rr, std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
                if (rr.IsOk())
                {
                    if (firstRelay[ch] == RelayState::Unknown) firstRelay[ch] = relay;
                    else if (firstRelay[ch] != relay) ++changes;
                }

                InputState input = InputState::Unknown;
                t0 = Clock::now();
                OperationResult ri = controller.QueryInputStatus(ch, input, 500);
                note(ri, std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
                if (ri.IsOk())
                {
                    if (firstInput[ch] == InputState::Unknown) firstInput[ch] = input;
                    else if (firstInput[ch] != input) ++changes;
                }
            }

            roundMs.push_back(std::chrono::duration<double, std::milli>(Clock::now() - roundStart).count());
        }
        const double wallS = std::chrono::duration<double>(Clock::now() - wallStart).count();

        const int total = rounds * 2 * ChannelCount;
        std::cout << std::fixed << std::setprecision(1);
        std::cout << "Queries answered : " << answered << " of " << total << "\n";
        std::cout << "Lost / failed    : " << timeouts + otherErrors << "  (" << timeouts << " timeouts, "
                  << otherErrors << " other errors)\n";
        std::cout << "State changes    : " << changes << "  (a relay or an input differed from its first reading)\n";

        if (!queryMs.empty())
        {
            std::sort(queryMs.begin(), queryMs.end());
            double sum = 0;
            for (double v : queryMs) sum += v;
            const double avg = sum / queryMs.size();
            const double p95 = queryMs[static_cast<size_t>(queryMs.size() * 0.95) < queryMs.size()
                                        ? static_cast<size_t>(queryMs.size() * 0.95) : queryMs.size() - 1];
            std::cout << "Query duration   : min " << queryMs.front() << " ms, average " << avg
                      << " ms, 95% under " << p95 << " ms, max " << queryMs.back() << " ms\n";
        }

        if (!roundMs.empty())
        {
            double sum = 0, worst = 0;
            for (double v : roundMs) { sum += v; worst = std::max(worst, v); }
            const double avgRound = sum / roundMs.size();
            const int pauseMs = 100; // StartPolling's default pause between rounds (the GUI uses 100 too)
            std::cout << "Round of " << 2 * ChannelCount << " queries: average " << avgRound << " ms, worst " << worst << " ms\n";
            std::cout << "\nWith the polling pause of " << pauseMs << " ms, a change on the board reaches the display "
                      << "after about " << (avgRound + pauseMs) / 2 << " ms on average and at most about "
                      << worst + pauseMs << " ms\n(plus 2 s for a relay switched by an input when the input relation "
                      << "is not known to the library).\n";
        }

        std::cout << "\nTotal time: " << wallS << " s.\n";
        return (timeouts + otherErrors) == 0 ? 0 : 2;
    }

    // Brackets a pulse duration from (delay, "relay still open when read") results.
    void PrintPulseSummary(const std::vector<std::pair<int, bool>>& results)
    {
        int lastOpen = -1;
        int firstClosed = -1;
        for (const auto& result : results)
        {
            if (result.second)
                lastOpen = std::max(lastOpen, result.first);
            else if (firstClosed < 0 || result.first < firstClosed)
                firstClosed = result.first;
        }

        std::cout << "\nResult: ";
        if (lastOpen < 0)
            std::cout << "the pulse was already over at the shortest delay tested.\n";
        else if (firstClosed < 0)
            std::cout << "the relay was still on at the longest delay tested (" << lastOpen
                      << " ms): this is not a short pulse.\n";
        else if (lastOpen < firstClosed)
            std::cout << "the pulse lasts between " << lastOpen << " and " << firstClosed
                      << " ms (give or take about 30 ms).\n";
        else
            std::cout << "inconsistent results (a longer delay found the relay open, a shorter one closed): repeat the test.\n";
    }

    // Measures how long a Momentary COMMAND pulse lasts. Reading the relay during a pulse cancels
    // it, so it cannot be watched by polling: instead, for each delay, send the command, read the
    // relay ONCE that long after it, then close it. Open = the pulse was still running.
    int RunPulseMeasure(Controller& controller, int channel, const std::vector<int>& delaysMs)
    {
        using Clock = std::chrono::steady_clock;
        int totalMs = 0;
        for (int d : delaysMs)
            totalMs += d + 600; // the delay itself, plus the close/settle steps around it
        std::cout << "Momentary pulse measurement on channel " << channel << ": nothing to do, about "
                  << (totalMs + 999) / 1000 << " s.\n\n";

        std::vector<std::pair<int, bool>> results;

        for (size_t i = 0; i < delaysMs.size(); ++i)
        {
            const int delay = delaysMs[i];

            controller.SendClose(channel);
            SleepMs(400);

            OperationResult started = controller.SendMomentary(channel);
            if (!started.IsOk())
            {
                std::cout << "Could not start the pulse (" << ResultCodeToString(started.code) << ") - aborting.\n";
                return 1;
            }
            const auto startedAt = Clock::now(); // the board answered: the pulse began a few ms before this

            std::this_thread::sleep_until(startedAt + std::chrono::milliseconds(delay));

            RelayState state = RelayState::Unknown;
            OperationResult r = controller.QueryRelayStatus(channel, state, 500);
            if (!r.IsOk())
            {
                std::cout << "Could not read the relay (" << ResultCodeToString(r.code) << ") - aborting.\n";
                return 1;
            }

            const bool stillOpen = (state == RelayState::Open);
            results.emplace_back(delay, stillOpen);
            std::cout << "[" << i + 1 << "/" << delaysMs.size() << "] read " << delay << " ms after the command: relay "
                      << (stillOpen ? "OPEN (pulse still running)" : "closed (pulse already over)") << "\n";

            controller.SendClose(channel);
        }

        PrintPulseSummary(results);
        return 0;
    }

    // Diagnostic for the input relation G3. In G3 the board pulses a relay by itself when
    // its input is grounded. Reading that relay during the pulse cancels it, so its duration
    // cannot be watched by polling: instead, for each requested delay, wait for the user to
    // ground the input (reading ONLY the input, which is harmless), read the relay ONCE that
    // long after the press, and close it again. "Open" means the pulse was still running at
    // that delay, "closed" that it was already over; the delays therefore bracket the duration.
    int RunInputPulseProbe(Controller& controller, int channel, std::vector<int> delaysMs)
    {
        using Clock = std::chrono::steady_clock;
        const int waitLimitMs = 30000;

        std::cout << "Input pulse probe on channel " << channel << ". Make sure the board is in G3 "
                     "(--set-relation 3).\nFor each line below, ground input " << channel
                  << " (then release it) when asked. Nothing else to do.\n\n";

        // Waits until the input reaches 'wanted'; returns the time it was first seen, or false on timeout/error.
        auto waitForInput = [&](InputState wanted, Clock::time_point& seenAt) -> bool
        {
            auto limit = Clock::now() + std::chrono::milliseconds(waitLimitMs);
            while (Clock::now() < limit)
            {
                InputState st = InputState::Unknown;
                if (!controller.QueryInputStatus(channel, st, 300).IsOk())
                    return false;
                if (st == wanted)
                {
                    seenAt = Clock::now();
                    return true;
                }
                SleepMs(5);
            }
            return false;
        };

        std::vector<std::pair<int, bool>> results; // (delay, relay still open)

        for (size_t i = 0; i < delaysMs.size(); ++i)
        {
            const int delay = delaysMs[i];

            controller.SendClose(channel);
            SleepMs(300);

            Clock::time_point seenAt;
            if (!waitForInput(InputState::Off, seenAt))
            {
                std::cout << "Input " << channel << " did not come back to rest - aborting.\n";
                return 1;
            }

            std::cout << "[" << i + 1 << "/" << delaysMs.size() << "] Ground input " << channel
                      << " now (the relay will be read " << delay << " ms after the press)... " << std::flush;

            if (!waitForInput(InputState::On, seenAt))
            {
                std::cout << "\nNo press seen - aborting.\n";
                return 1;
            }

            std::this_thread::sleep_until(seenAt + std::chrono::milliseconds(delay));

            RelayState state = RelayState::Unknown;
            OperationResult r = controller.QueryRelayStatus(channel, state, 500);
            if (!r.IsOk())
            {
                std::cout << "\nCould not read the relay (" << ResultCodeToString(r.code) << ") - aborting.\n";
                return 1;
            }

            const bool stillOpen = (state == RelayState::Open);
            results.emplace_back(delay, stillOpen);
            std::cout << (stillOpen ? "relay OPEN (pulse still running)" : "relay closed (pulse already over)") << "\n";

            controller.SendClose(channel); // the read may have cancelled the pulse: leave the relay closed
        }

        PrintPulseSummary(results);

        return 0;
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
        else if (cmd == "--stress-test")
        {
            int rounds = (remaining.size() >= 2) ? std::atoi(remaining[1].c_str()) : 50;
            if (rounds < 1 || rounds > 1000) { std::cerr << "Rounds must be 1-1000\n"; return 1; }
            return RunStressTest(controller, rounds);
        }
        else if (cmd == "--measure-pulse")
        {
            if (remaining.size() < 2) { std::cerr << "Usage: --measure-pulse <channel> [delay_ms ...]\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());
            if (channel < 1 || channel > ChannelCount) { std::cerr << "Channel must be 1-8\n"; return 1; }

            std::vector<int> delays;
            for (size_t j = 2; j < remaining.size(); ++j)
                delays.push_back(std::atoi(remaining[j].c_str()));
            if (delays.empty())
                delays = { 300, 450, 600, 800, 1000, 1500 };

            return RunPulseMeasure(controller, channel, delays);
        }
        else if (cmd == "--probe-input-pulse")
        {
            if (remaining.size() < 2) { std::cerr << "Usage: --probe-input-pulse <channel> [delay_ms ...]\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());
            if (channel < 1 || channel > ChannelCount) { std::cerr << "Channel must be 1-8\n"; return 1; }

            std::vector<int> delays;
            for (size_t j = 2; j < remaining.size(); ++j)
                delays.push_back(std::atoi(remaining[j].c_str()));
            if (delays.empty())
                delays = { 300, 600, 900, 1300, 2000, 4000 };

            return RunInputPulseProbe(controller, channel, delays);
        }
        else if (cmd == "--probe-pulse")
        {
            if (remaining.size() < 2) { std::cerr << "Usage: --probe-pulse <channel> [delay <seconds>]\n"; return 1; }
            int channel = std::atoi(remaining[1].c_str());
            if (channel < 1 || channel > ChannelCount) { std::cerr << "Channel must be 1-8\n"; return 1; }

            bool useDelay = (remaining.size() >= 3 && remaining[2] == "delay");
            int delaySeconds = (useDelay && remaining.size() >= 4) ? std::atoi(remaining[3].c_str()) : 3;
            if (useDelay && !controller.SupportsDelay())
            {
                std::cerr << "The Delay command is only available with --protocol at\n";
                return 1;
            }
            if (useDelay && (delaySeconds < 1 || delaySeconds > 60))
            {
                std::cerr << "For this probe the delay must be 1-60 seconds\n";
                return 1;
            }
            return RunPulseProbe(controller, channel, useDelay, delaySeconds);
        }
        else if (cmd == "--monitor-inputs")
        {
            int intervalMs = 200;
            int relation = -1; // unknown
            ChannelMask relayMask = AllChannels;
            ChannelMask inputMask = AllChannels;
            for (size_t j = 1; j < remaining.size(); ++j)
            {
                if (remaining[j] == "--interval" && j + 1 < remaining.size())
                    intervalMs = std::atoi(remaining[++j].c_str());
                else if (remaining[j] == "--relation" && j + 1 < remaining.size())
                    relation = std::atoi(remaining[++j].c_str());
                else if ((remaining[j] == "--relays" || remaining[j] == "--inputs") && j + 1 < remaining.size())
                {
                    const bool isRelays = (remaining[j] == "--relays");
                    if (!ParseChannelList(remaining[j + 1], isRelays ? relayMask : inputMask))
                    {
                        std::cerr << "Invalid channel list for " << remaining[j] << ": '" << remaining[j + 1]
                                  << "' (use all, none, or e.g. 1,2,5-7)\n";
                        return 1;
                    }
                    ++j;
                }
            }

            if (relation >= 0 && relation <= 3)
            {
                // The board remembers its mode but cannot be asked for it, so the program is told.
                controller.SetKnownInputOutputRelation(static_cast<InputOutputRelation>(relation));
            }
            else
            {
                std::cout << "Note: the input relation is not specified (--relation 0-3). Relays are read with a "
                             "best-effort protection that can still cancel a pulse\n"
                             "the board starts by itself in G3, and relay changes caused by an input show up "
                             "about 2 s late.\n";
            }

            controller.SetEventCallback([](const Event& ev) {
                if (ev.type == EventType::InputStateChanged)
                {
                    std::cout << "Input " << ev.channel << " -> "
                              << (ev.inputState == InputState::On ? "On" : "Off") << "\n";
                }
                else if (ev.type == EventType::RelayStateChanged)
                {
                    std::cout << "Relay " << ev.channel << " -> "
                              << (ev.relayState == RelayState::Open ? "ON (open)" : "OFF (closed)") << "\n";
                }
                else if (ev.type == EventType::TransportError)
                {
                    // Polling has already stopped by itself at this point.
                    std::cout << "Connection lost (" << ResultCodeToString(ev.errorCode)
                              << "): polling stopped. Press Enter to quit.\n";
                }
            });

            std::cout << "Monitoring every " << intervalMs << " ms (relays mask 0x" << std::hex << std::uppercase
                      << std::setw(2) << std::setfill('0') << static_cast<int>(relayMask)
                      << ", inputs mask 0x" << std::setw(2) << static_cast<int>(inputMask) << std::dec
                      << std::setfill(' ') << "). Press Enter to stop.\n";
            controller.StartPolling(intervalMs, relayMask, inputMask);
            std::cin.get();
            controller.StopPolling();
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
