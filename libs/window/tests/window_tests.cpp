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
#include "window/window.hpp"
#include "window/window_event.hpp"
#include <cstdint>
#include <vector>

namespace
{

    /*!
        The smallest possible Window: platform members stubbed to nothing, so the base class's own
        contract — the queue, the immediate callback, the flags — can be exercised without a display.
        `push` re-exposes the protected `pushEvent`, standing in for the platform's event handler.
    */
    class StubWindow final : public WindowLib::Window {
    public:
        explicit StubWindow(LoggingLib::Logger& logger) :
            Window(logger)
        {
        }

        void pumpEvents() override
        {
        }

        void waitEvents() override
        {
        }

        void wakeEvents() override
        {
        }

        void setCursorCaptured(bool captured) override
        {
            m_cursor_captured = captured;
        }

        [[nodiscard]] void* nativeHandle() const override
        {
            return nullptr;
        }

        [[nodiscard]] void* nativeDisplay() const override
        {
            return nullptr;
        }

        void push(const WindowLib::WindowEvent& ev)
        {
            pushEvent(ev);
        }
    };

} // namespace

TEST_CASE(push_retains_events_in_fifo_order)
{
    LoggingLib::Logger logger;
    StubWindow window{logger};

    WindowLib::WindowEvent key{WindowLib::WindowEvent::Type::KeyDown};
    key.key.keycode = 1u;
    window.push(key);
    key.key.keycode = 2u;
    window.push(key);
    key.key.keycode = 3u;
    window.push(key);

    WindowLib::WindowEvent out{};
    TEST_CHECK(window.pollEvent(out));
    TEST_CHECK_EQUAL(out.key.keycode, 1u);
    TEST_CHECK(window.pollEvent(out));
    TEST_CHECK_EQUAL(out.key.keycode, 2u);
    TEST_CHECK(window.pollEvent(out));
    TEST_CHECK_EQUAL(out.key.keycode, 3u);
    TEST_CHECK(!window.pollEvent(out));
}

TEST_CASE(poll_on_empty_queue_returns_false)
{
    LoggingLib::Logger logger;
    StubWindow window{logger};

    WindowLib::WindowEvent out{};
    TEST_CHECK(!window.pollEvent(out));
}

TEST_CASE(callback_fires_immediately_and_queue_still_retains)
{
    /*
        The contract the renderer's threading depends on: the callback fires from inside the push —
        that is what keeps events flowing during a Win32 modal drag — and the event still lands in
        the queue afterwards, because the event thread drains it separately for the close request.
        One or the other alone would pass a weaker test.
    */
    LoggingLib::Logger logger;
    StubWindow window{logger};

    std::vector<uint32_t> seen;
    window.setEventCallback(
        [](const WindowLib::WindowEvent& ev, void* user_data) {
            static_cast<std::vector<uint32_t>*>(user_data)->push_back(ev.key.keycode);
        },
        &seen);

    WindowLib::WindowEvent key{WindowLib::WindowEvent::Type::KeyDown};
    key.key.keycode = 42u;
    window.push(key);

    TEST_CHECK_EQUAL(seen.size(), 1u);
    TEST_CHECK_EQUAL(seen.front(), 42u);

    WindowLib::WindowEvent out{};
    TEST_CHECK(window.pollEvent(out));
    TEST_CHECK_EQUAL(out.key.keycode, 42u);
}

TEST_CASE(resetting_the_callback_to_null_stops_delivery)
{
    // The event thread does exactly this at shutdown, before the channel it forwards into dies.
    LoggingLib::Logger logger;
    StubWindow window{logger};

    std::vector<uint32_t> seen;
    window.setEventCallback(
        [](const WindowLib::WindowEvent& ev, void* user_data) {
            static_cast<std::vector<uint32_t>*>(user_data)->push_back(ev.key.keycode);
        },
        &seen);
    window.setEventCallback(nullptr, nullptr);

    WindowLib::WindowEvent key{WindowLib::WindowEvent::Type::KeyDown};
    key.key.keycode = 7u;
    window.push(key);

    TEST_CHECK(seen.empty());

    // The queue is unaffected by the callback's absence.
    WindowLib::WindowEvent out{};
    TEST_CHECK(window.pollEvent(out));
}

TEST_CASE(request_close_sets_should_close)
{
    LoggingLib::Logger logger;
    StubWindow window{logger};

    TEST_CHECK(!window.shouldClose());
    window.requestClose();
    TEST_CHECK(window.shouldClose());
}

TEST_CASE(default_event_is_none)
{
    WindowLib::WindowEvent ev;
    TEST_CHECK(ev.type == WindowLib::WindowEvent::Type::None);
}

TEST_CASE(explicit_event_type)
{
    WindowLib::WindowEvent ev{WindowLib::WindowEvent::Type::Close};
    TEST_CHECK(ev.type == WindowLib::WindowEvent::Type::Close);
}

TEST_CASE(resize_event_data)
{
    WindowLib::WindowEvent ev{WindowLib::WindowEvent::Type::Resize};
    ev.resize.width = 1920;
    ev.resize.height = 1080;
    TEST_CHECK_EQUAL(ev.resize.width, static_cast<uint32_t>(1920));
    TEST_CHECK_EQUAL(ev.resize.height, static_cast<uint32_t>(1080));
}

TEST_CASE(key_event_data)
{
    WindowLib::WindowEvent ev{WindowLib::WindowEvent::Type::KeyDown};
    ev.key.keycode = 27;
    ev.key.repeat = false;
    TEST_CHECK_EQUAL(ev.key.keycode, static_cast<uint32_t>(27));
    TEST_CHECK(!ev.key.repeat);
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
