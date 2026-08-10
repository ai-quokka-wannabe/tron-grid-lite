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

#include "logging/logger.hpp"
#include <iostream>
#include <queue>
#include <thread>

namespace LoggingLib
{

    //! Convert severity to a human-readable prefix string.
    [[nodiscard]] static std::string_view severityPrefix(Severity severity)
    {
        switch (severity) {
        case Severity::Debug:
            return "[DEBUG]";
        case Severity::Info:
            return "[INFO]";
        case Severity::Warning:
            return "[WARNING]";
        case Severity::Error:
            return "[ERROR]";
        case Severity::Fatal:
            return "[FATAL]";
        }
        return "[UNKNOWN]";
    }

    //! Write one message to its stream: Warning and above to stderr, everything else to stdout.
    static void writeMessage(const LogMessage& msg)
    {
        std::string_view prefix{severityPrefix(msg.severity)};
        if (msg.severity >= Severity::Warning) {
            std::cerr << prefix << " " << msg.text << "\n";
        } else {
            std::cout << prefix << " " << msg.text << "\n";
        }
    }

    Logger::Logger() :
        m_worker([this](std::stop_token token) {
            workerLoop(token);
        })
    {
    }

    Logger::~Logger()
    {
        // Request stop. The std::condition_variable_any::wait overload that takes a
        // stop_token wakes the worker on request_stop() automatically — no manual
        // notify_one() needed. The jthread destructor then joins; the worker drains
        // remaining messages before returning.
        m_worker.request_stop();
    }

    void Logger::logDebug(std::string_view message)
    {
        enqueue(Severity::Debug, message);
    }

    void Logger::logInfo(std::string_view message)
    {
        enqueue(Severity::Info, message);
    }

    void Logger::logWarning(std::string_view message)
    {
        enqueue(Severity::Warning, message);
    }

    void Logger::logError(std::string_view message)
    {
        enqueue(Severity::Error, message);
    }

    void Logger::logFatal(std::string_view message)
    {
        /*
            Everything the program logged on its way here is still sitting in the queue, and the
            caller is almost certainly about to abort — which joins no threads and is not required
            to flush the standard streams. Draining first means the fatal line arrives last, after
            the context that explains it, rather than first and alone.
        */
        flush();

        // Written directly to stderr rather than queued: this must be visible before the process
        // dies, and there may be no worker left to drain a queue by then.
        std::cerr << severityPrefix(Severity::Fatal) << " " << message << "\n";
        std::cerr.flush();
    }

    void Logger::flush()
    {
        /*
            Waits until every message enqueued before this call has been written, rather than until
            the queue is empty: emptiness only proves the worker has taken a message, not that it
            has finished writing it, and a fatal line written after an emptiness check races the
            worker's last write mid-line — observed in practice as "[ERROR] [FATAL] ..." on one
            line. The counters close that window, because the worker increments m_written only
            after a message's stream write has completed.

            The wait must not be unconditional. The worker's catch-all means it can have died —
            std::bad_alloc under memory pressure — leaving the count short forever, and an
            unconditional wait would turn logFatal into a hang on the very path whose job is to end
            the process. A dead worker writes nothing more, so whatever is still queued is written
            here instead, with no interleaving left to fear.
        */
        const std::size_t target{m_enqueued.load(std::memory_order_acquire)};
        while ((m_written.load(std::memory_order_acquire) < target) && !m_worker_exited.load(std::memory_order_acquire)) {
            m_cv.notify_one();
            std::this_thread::yield();
        }

        if (m_worker_exited.load(std::memory_order_acquire)) {
            std::queue<LogMessage> remainder{m_queue.drain()};
            while (!remainder.empty()) {
                writeMessage(remainder.front());
                remainder.pop();
            }
        }

        std::cout.flush();
    }

    void Logger::enqueue(Severity severity, std::string_view message)
    {
        // The C++ memory model requires that any state read by the wait predicate be
        // modified under the same mutex used by the wait. Without this, a notification
        // emitted after the worker has checked the predicate (true → empty) but before
        // the worker has registered as a waiter inside cv.wait() is lost — the worker
        // would sleep indefinitely while messages pile up. Holding m_mutex around the
        // emit closes the race; notify_one() is intentionally outside the lock so the
        // woken worker doesn't immediately re-block on the mutex we just released.
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_queue.emit({severity, std::string(message)});
            m_enqueued.fetch_add(1, std::memory_order_release);
        }
        m_cv.notify_one();
    }

    void Logger::workerLoop(std::stop_token stop_token)
    {
        /*
            The whole body is inside a catch-all because this is a thread's entry point, and an
            exception leaving one of those calls std::terminate outright — no handler anywhere in the
            process can intervene. The work below looks incapable of throwing and is not: allocating
            a message string can throw std::bad_alloc, and locking can report through
            std::system_error.

            The irony is the reason it matters. This thread exists to report what went wrong, so
            without this the first symptom of memory pressure would be the process dying without a
            word — in the component whose entire job is to have the last word.

            A logger that has stopped logging is the correct outcome here. The alternative is a
            logger that has stopped the program.
        */
        try {
            drainUntilStopped(stop_token);
        }
        // NOLINTNEXTLINE(bugprone-empty-catch) — reporting is exactly what has just failed.
        catch (...) {
        }

        // Last statement on this thread, so a true flag means no further write can happen —
        // which is what licenses flush() to write the remainder itself.
        m_worker_exited.store(true, std::memory_order_release);
    }

    void Logger::drainUntilStopped(std::stop_token stop_token)
    {
        while (!stop_token.stop_requested()) {
            // Wait for messages or stop — the CV checks stop_token automatically
            {
                std::unique_lock<std::mutex> lock{m_mutex};
                m_cv.wait(lock, stop_token, [this]() {
                    return !m_queue.empty();
                });
            }
            // Lock released before draining — no lock ordering issue with Signal's mutex

            writeAll(m_queue.drain());
        }

        // Final drain — catch messages emitted between the last check and stop.
        writeAll(m_queue.drain());
    }

    void Logger::writeAll(std::queue<LogMessage> batch)
    {
        while (!batch.empty()) {
            writeMessage(batch.front());
            // Incremented only after the write above has completed, because flush() reads this
            // count as "safe to write the fatal line without racing the worker mid-line".
            m_written.fetch_add(1, std::memory_order_release);
            batch.pop();
        }
    }

} // namespace LoggingLib
