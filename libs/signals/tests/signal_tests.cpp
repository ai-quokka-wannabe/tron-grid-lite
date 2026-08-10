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
#include "signal/signal.hpp"
#include <memory>
#include <queue>
#include <thread>

TEST_CASE(emit_and_consume)
{
    SignalsLib::Signal<int> sig;
    sig.emit(42);

    int value{0};
    TEST_CHECK(sig.consume(value));
    TEST_CHECK_EQUAL(value, 42);
}

TEST_CASE(consume_empty_returns_false)
{
    SignalsLib::Signal<int> sig;

    int value{0};
    TEST_CHECK(!sig.consume(value));
}

TEST_CASE(fifo_order)
{
    SignalsLib::Signal<int> sig;
    sig.emit(1);
    sig.emit(2);
    sig.emit(3);

    int value{0};
    (void)sig.consume(value);
    TEST_CHECK_EQUAL(value, 1);
    (void)sig.consume(value);
    TEST_CHECK_EQUAL(value, 2);
    (void)sig.consume(value);
    TEST_CHECK_EQUAL(value, 3);
}

TEST_CASE(drain_returns_all_in_order_and_leaves_signal_empty)
{
    SignalsLib::Signal<int> sig;
    sig.emit(1);
    sig.emit(2);
    sig.emit(3);

    std::queue<int> batch{sig.drain()};
    TEST_CHECK(sig.empty());
    TEST_CHECK_EQUAL(batch.size(), static_cast<std::size_t>(3));

    TEST_CHECK_EQUAL(batch.front(), 1);
    batch.pop();
    TEST_CHECK_EQUAL(batch.front(), 2);
    batch.pop();
    TEST_CHECK_EQUAL(batch.front(), 3);
}

TEST_CASE(drain_empty_returns_empty_queue)
{
    SignalsLib::Signal<int> sig;

    std::queue<int> batch{sig.drain()};
    TEST_CHECK(batch.empty());
}

TEST_CASE(empty_and_size)
{
    SignalsLib::Signal<int> sig;
    TEST_CHECK(sig.empty());
    TEST_CHECK_EQUAL(sig.size(), static_cast<std::size_t>(0));

    sig.emit(1);
    TEST_CHECK(!sig.empty());
    TEST_CHECK_EQUAL(sig.size(), static_cast<std::size_t>(1));
}

TEST_CASE(weak_ptr_ownership_model)
{
    std::shared_ptr<SignalsLib::Signal<int>> sig{std::make_shared<SignalsLib::Signal<int>>()};
    std::weak_ptr<SignalsLib::Signal<int>> weak{sig};

    sig->emit(99);
    TEST_CHECK(!weak.expired());

    sig.reset();
    TEST_CHECK(weak.expired());
}

TEST_CASE(thread_safety)
{
    SignalsLib::Signal<int> sig;
    constexpr int count{1000};

    std::thread producer([&] {
        for (int i{0}; i < count; ++i) {
            sig.emit(i);
        }
    });

    // A single producer means the per-producer FIFO guarantee is a global order, so the consumer
    // can assert every value rather than merely counting them.
    bool in_order{true};
    std::thread consumer([&] {
        int consumed{0};
        while (consumed < count) {
            int value{0};
            if (sig.consume(value)) {
                if (value != consumed) {
                    in_order = false;
                }
                ++consumed;
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_CHECK(in_order);
    TEST_CHECK(sig.empty());
}

TEST_CASE(thread_safety_drain)
{
    SignalsLib::Signal<int> sig;
    constexpr int count{1000};

    std::thread producer([&] {
        for (int i{0}; i < count; ++i) {
            sig.emit(i);
        }
    });

    bool in_order{true};
    std::thread consumer([&] {
        int consumed{0};
        while (consumed < count) {
            std::queue<int> batch{sig.drain()};
            while (!batch.empty()) {
                if (batch.front() != consumed) {
                    in_order = false;
                }
                batch.pop();
                ++consumed;
            }
        }
    });

    producer.join();
    consumer.join();

    TEST_CHECK(in_order);
    TEST_CHECK(sig.empty());
}

TEST_CASE(fifo_per_producer_with_concurrent_producers)
{
    /*
        FIFO is guaranteed per producer, and that guarantee is what stands between a careless emit
        and the replay guarantee (docs/ARCHITECTURE.md). Two producers emit tagged ascending
        sequences; whatever interleaving the scheduler produces, each tag's values must arrive in
        emit order.
    */
    struct Tagged {
        int producer{0};
        int sequence{0};
    };

    SignalsLib::Signal<Tagged> sig;
    constexpr int count{1000};

    std::thread first([&] {
        for (int i{0}; i < count; ++i) {
            sig.emit({0, i});
        }
    });
    std::thread second([&] {
        for (int i{0}; i < count; ++i) {
            sig.emit({1, i});
        }
    });

    bool in_order{true};
    std::thread consumer([&] {
        int next_expected[2]{0, 0};
        int consumed{0};
        while (consumed < 2 * count) {
            Tagged value{};
            if (sig.consume(value)) {
                if (value.sequence != next_expected[value.producer]) {
                    in_order = false;
                }
                ++next_expected[value.producer];
                ++consumed;
            }
        }
    });

    first.join();
    second.join();
    consumer.join();

    TEST_CHECK(in_order);
    TEST_CHECK(sig.empty());
}

int main()
{
    return static_cast<int>(TestingLib::runAll());
}
