/*
    Copyright (C) 2026 Matej Gomboc https://github.com/ai-quokka-wannabe/tron-grid-lite

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#pragma once

#include <signal/signal.hpp>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>

namespace LoggingLib
{

    //! Message severity levels.
    enum class Severity {
        Debug, //!< Verbose diagnostic information.
        Info, //!< Normal operational messages.
        Warning, //!< Potential issues that do not prevent operation.
        Error, //!< Failures that prevent a specific operation.
        Fatal //!< Unrecoverable failures — the application will terminate.
    };

    //! A single log message with severity and text.
    struct LogMessage {
        Severity severity{Severity::Info}; //!< Severity level.
        std::string text; //!< Message content.
    };

    /*!
        Thread-safe logger that writes messages on a background thread.

        Uses SignalsLib::Signal<LogMessage> as the internal queue. Any thread
        can call logDebug(), logInfo(), etc. The background worker drains
        the queue and writes to stdout (Debug, Info) or stderr (Warning,
        Error).

        Fatal is the exception: it is written synchronously to stderr rather
        than queued, because it exists to be read immediately before the
        process dies. That makes it arrive out of order with respect to
        anything still queued, so logFatal() flushes the queue first.

        RAII lifecycle: std::jthread auto-joins in the destructor and
        signals the stop_token to wake the worker.
    */
    class Logger {
    public:
        //! Spawn the background worker thread.
        Logger();

        //! Destructor — std::jthread auto-joins and signals stop.
        ~Logger();

        //! Non-copyable, non-movable (owns a thread).
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        //! Log a debug message.
        void logDebug(std::string_view message);

        //! Log an informational message.
        void logInfo(std::string_view message);

        //! Log a warning message.
        void logWarning(std::string_view message);

        //! Log an error message.
        void logError(std::string_view message);

        //! Log a fatal error message. Writes directly to stderr (synchronous).
        void logFatal(std::string_view message);

        /*!
            Blocks until every message queued before the call has been written, then flushes stdout.

            std::abort() joins no threads, runs no static destructors and is not required to flush
            the standard streams — glibc and the MSVC CRT do not. Without this, everything the
            program logged on its way to a fatal error is silently discarded, leaving a user with a
            single line of context and no idea what led to it.

            If the worker thread has died — its catch-all exists because allocation can throw —
            whatever is still queued is written from the calling thread instead, so this returns in
            bounded time rather than waiting on a thread that will never drain again.

            Called automatically by logFatal(), so a caller that is about to abort need do nothing.
        */
        void flush();

    private:
        //! Push a message onto the queue and wake the worker.
        void enqueue(Severity severity, std::string_view message);

        //! Worker thread entry point — drains the queue until stop is requested.
        /*!
            The worker thread's entry point, and nothing more than a catch-all around the drain.

            An exception leaving a thread's entry point calls `std::terminate` with no handler able
            to intervene, so the boundary is kept separate from the work to make it obvious that
            nothing may be added outside it.
        */
        void workerLoop(std::stop_token stop_token);

        //! The actual draining. May throw; `workerLoop` is where that stops being survivable.
        void drainUntilStopped(std::stop_token stop_token);

        //! Writes a drained batch in order, incrementing m_written after each completed write.
        void writeAll(std::queue<LogMessage> batch);

        SignalsLib::Signal<LogMessage> m_queue; //!< Thread-safe message queue.
        std::mutex m_mutex; //!< Protects the wake-up condition.
        std::condition_variable_any m_cv; //!< Wakes the worker when messages arrive.
        std::atomic<std::size_t> m_enqueued{0}; //!< Messages queued so far; incremented under m_mutex in enqueue().
        std::atomic<std::size_t> m_written{0}; //!< Messages whose stream write has completed; flush() waits on this.
        std::atomic<bool> m_worker_exited{false}; //!< Set by the worker on exit; flush() must not wait on a dead worker.
        std::jthread m_worker; //!< Background writer thread (auto-joins on destruction). Declared last: it starts a thread that uses the members above.
    };

} // namespace LoggingLib
