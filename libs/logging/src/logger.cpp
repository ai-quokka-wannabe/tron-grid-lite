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

#include "log/logger.hpp"
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
        std::cerr << "[FATAL] " << message << "\n";
        std::cerr.flush();
    }

    void Logger::flush()
    {
        /*
            Waits for the worker to empty the queue rather than draining it here, because two
            threads writing the same stream would interleave mid-line.

            This guarantees nothing is left unconsumed; it does not synchronise with the worker's
            final write, which may still be in flight for a few microseconds after the queue reports
            empty. Closing that window would need a second condition variable for a pre-abort path
            that is already about to lose the process, which is not a trade this repository makes.
        */
        while (!m_queue.empty()) {
            m_cv.notify_one();
            std::this_thread::yield();
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

            /*
                One message per lock rather than Signal::drain(), deliberately: flush() reads the
                queue's emptiness as its proxy for "everything written", and a batch swap would
                report empty while a whole batch sat unwritten in this loop's hands — widening
                flush's documented in-flight window from one message to a batch.
            */
            LogMessage msg{};
            while (m_queue.consume(msg)) {
                std::string_view prefix{severityPrefix(msg.severity)};
                if (msg.severity >= Severity::Warning) {
                    std::cerr << prefix << " " << msg.text << "\n";
                } else {
                    std::cout << prefix << " " << msg.text << "\n";
                }
            }
        }

        // Final drain — catch messages emitted between last check and stop. A batch swap is safe
        // here where it is not above: stop has been requested, so nothing can call flush() against
        // a queue that reports empty while the batch is still being written.
        std::queue<LogMessage> batch{m_queue.drain()};
        while (!batch.empty()) {
            const LogMessage& msg{batch.front()};
            std::string_view prefix{severityPrefix(msg.severity)};
            if (msg.severity >= Severity::Warning) {
                std::cerr << prefix << " " << msg.text << "\n";
            } else {
                std::cout << prefix << " " << msg.text << "\n";
            }
            batch.pop();
        }
    }

} // namespace LoggingLib
