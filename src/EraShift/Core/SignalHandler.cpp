#include "EraShift/Core/SignalHandler.hpp"

#include <csignal>

namespace EraShift::Core {
namespace {

// A process has exactly one set of signal dispositions, so this state is
// process-wide by definition rather than by convenience.
volatile std::sig_atomic_t g_shutdownRequested = 0;
volatile std::sig_atomic_t g_shutdownSignal    = 0;

/// The only thing the handler is allowed to do: assign to a sig_atomic_t.
extern "C" void handleShutdownSignal(int signalNumber)
{
    g_shutdownSignal    = signalNumber;
    g_shutdownRequested = 1;
}

} // namespace

bool installShutdownHandlers() noexcept
{
    // SIGHUP already terminates by default, but it is handled here too so that
    // a terminal closing shuts the game down as politely as Ctrl-C.
    const std::sig_atomic_t kHandled[] = {SIGINT, SIGTERM, SIGHUP};
    bool allInstalled = true;

    for (const std::sig_atomic_t signalNumber : kHandled) {
        struct sigaction action {};
        action.sa_handler = &handleShutdownSignal;
        sigemptyset(&action.sa_mask);
        // No SA_RESTART: a blocking call should return EINTR so the main loop
        // notices the request promptly.
        action.sa_flags = 0;

        if (sigaction(signalNumber, &action, nullptr) != 0) {
            allInstalled = false;
        }
    }

    return allInstalled;
}

bool shutdownRequested() noexcept
{
    return g_shutdownRequested != 0;
}

void clearShutdownRequest() noexcept
{
    g_shutdownRequested = 0;
    g_shutdownSignal    = 0;
}

const char* shutdownSignalName() noexcept
{
    switch (g_shutdownSignal) {
        case SIGINT:  return "SIGINT";
        case SIGTERM: return "SIGTERM";
        case SIGHUP:  return "SIGHUP";
        default:      return "";
    }
}

} // namespace EraShift::Core
