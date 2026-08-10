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

#include "testing/testing.hpp"
#include "logging/logger.hpp"
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace
{

    /*!
        Redirects std::cout and std::cerr into string buffers for the lifetime of the object.

        Constructed before the Logger and read after the Logger's destruction: the destructor joins
        the worker, which is what guarantees every write has landed in the buffers before they are
        inspected. Reading them while a Logger is alive is meaningful only for writes documented as
        synchronous, which is logFatal's.
    */
    class CaptureStreams {
    public:
        CaptureStreams() :
            m_old_cout{std::cout.rdbuf(m_cout.rdbuf())},
            m_old_cerr{std::cerr.rdbuf(m_cerr.rdbuf())}
        {
        }

        ~CaptureStreams()
        {
            std::cout.rdbuf(m_old_cout);
            std::cerr.rdbuf(m_old_cerr);
        }

        CaptureStreams(const CaptureStreams&) = delete;
        CaptureStreams& operator=(const CaptureStreams&) = delete;
        CaptureStreams(CaptureStreams&&) = delete;
        CaptureStreams& operator=(CaptureStreams&&) = delete;

        [[nodiscard]] std::string coutText() const
        {
            return m_cout.str();
        }

        [[nodiscard]] std::string cerrText() const
        {
            return m_cerr.str();
        }

    private:
        std::ostringstream m_cout;
        std::ostringstream m_cerr;
        std::streambuf* m_old_cout;
        std::streambuf* m_old_cerr;
    };

    //! Counts non-overlapping occurrences of `needle` in `haystack`.
    [[nodiscard]] std::size_t countOccurrences(const std::string& haystack, const std::string& needle)
    {
        std::size_t count{0};
        std::size_t pos{haystack.find(needle)};
        while (pos != std::string::npos) {
            ++count;
            pos = haystack.find(needle, pos + needle.size());
        }
        return count;
    }

} // namespace

TEST_CASE(logger_construct_destroy)
{
    LoggingLib::Logger logger;
    // Constructor spawns worker, destructor joins — no crash.
}

TEST_CASE(severity_routes_to_the_documented_stream_with_its_prefix)
{
    std::string out;
    std::string err;
    {
        CaptureStreams capture;
        {
            LoggingLib::Logger logger;
            logger.logDebug("watch closely");
            logger.logInfo("all is well");
            logger.logWarning("this may bite");
            logger.logError("this bit");
        }
        out = capture.coutText();
        err = capture.cerrText();
    }

    TEST_CHECK(out.find("[DEBUG] watch closely\n") != std::string::npos);
    TEST_CHECK(out.find("[INFO] all is well\n") != std::string::npos);
    TEST_CHECK(err.find("[WARNING] this may bite\n") != std::string::npos);
    TEST_CHECK(err.find("[ERROR] this bit\n") != std::string::npos);

    // Routing means absence too: a message on both streams, or on the wrong one, passes the four
    // checks above.
    TEST_CHECK(out.find("[WARNING]") == std::string::npos);
    TEST_CHECK(out.find("[ERROR]") == std::string::npos);
    TEST_CHECK(err.find("[DEBUG]") == std::string::npos);
    TEST_CHECK(err.find("[INFO]") == std::string::npos);
}

TEST_CASE(messages_from_one_thread_arrive_in_order)
{
    std::string out;
    {
        CaptureStreams capture;
        {
            LoggingLib::Logger logger;
            logger.logInfo("first");
            logger.logInfo("second");
            logger.logInfo("third");
        }
        out = capture.coutText();
    }

    const std::size_t first{out.find("first")};
    const std::size_t second{out.find("second")};
    const std::size_t third{out.find("third")};
    TEST_CHECK(first != std::string::npos && second != std::string::npos && third != std::string::npos);
    TEST_CHECK(first < second);
    TEST_CHECK(second < third);
}

TEST_CASE(fatal_writes_to_stderr_synchronously)
{
    std::string err_during_lifetime;
    {
        CaptureStreams capture;
        {
            LoggingLib::Logger logger;
            logger.logFatal("the last word");
            // Read while the logger is alive: logFatal documents a synchronous write, so the text
            // must already be in the buffer, no join needed.
            err_during_lifetime = capture.cerrText();
        }
    }

    TEST_CHECK(err_during_lifetime.find("[FATAL] the last word\n") != std::string::npos);
}

TEST_CASE(fatal_arrives_after_the_context_that_explains_it)
{
    std::string err;
    {
        CaptureStreams capture;
        {
            LoggingLib::Logger logger;
            logger.logWarning("the context");
            logger.logFatal("the conclusion");
            // logFatal waits for every earlier message's write to complete before writing its own
            // line, so this ordering is deterministic, not a race the test usually wins.
            err = capture.cerrText();
        }
    }

    const std::size_t context{err.find("[WARNING] the context\n")};
    const std::size_t conclusion{err.find("[FATAL] the conclusion\n")};
    TEST_CHECK(context != std::string::npos);
    TEST_CHECK(conclusion != std::string::npos);
    TEST_CHECK(context < conclusion);
}

TEST_CASE(concurrent_producers_lose_nothing)
{
    constexpr int count{100};

    std::string out;
    std::string err;
    {
        CaptureStreams capture;
        {
            LoggingLib::Logger logger;

            std::thread t1([&] {
                for (int i{0}; i < count; ++i) {
                    logger.logInfo("from thread one");
                }
            });

            std::thread t2([&] {
                for (int i{0}; i < count; ++i) {
                    logger.logWarning("from thread two");
                }
            });

            t1.join();
            t2.join();
        }
        out = capture.coutText();
        err = capture.cerrText();
    }

    TEST_CHECK_EQUAL(countOccurrences(out, "from thread one"), count);
    TEST_CHECK_EQUAL(countOccurrences(err, "from thread two"), count);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
