// Era Shift - shutdown signal handling.
//
// Pressing Ctrl-C, sending SIGTERM (`kill`, `timeout`, CI teardown) or
// closing the terminal must end the game cleanly: resources released, states
// given their onExit(), logs flushed. Default disposition would terminate the
// process mid-frame, leaking GPU resources and cutting the log off mid-line.
//
// The handler itself does nothing but set a flag. Doing real work inside a
// signal handler is unsafe (it runs on an interrupted stack, and anything it
// calls may not be async-signal-safe), so the flag is polled by the main loop
// where normal cleanup is legal.
//
// `volatile sig_atomic_t` is the only object type the standard guarantees is
// safe to touch from a handler.

#pragma once

namespace EraShift::Core {

/// Installs handlers for SIGINT, SIGTERM and SIGHUP.
///
/// Safe to call more than once. Returns false if the handlers could not be
/// installed, in which case the game still runs and simply cannot be
/// interrupted politely.
bool installShutdownHandlers() noexcept;

/// True once a shutdown signal has been received.
[[nodiscard]] bool shutdownRequested() noexcept;

/// Clears the flag. Used by tests so one run does not affect the next.
void clearShutdownRequest() noexcept;

/// Name of the signal that triggered the shutdown, or "" if none. Safe to call
/// only from the main thread.
[[nodiscard]] const char* shutdownSignalName() noexcept;

} // namespace EraShift::Core
