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

#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>

namespace SignalsLib
{

    /*!
        Thread-safe, typed message queue for inter-system communication.

        A mutex and a `std::queue`, and deliberately nothing else. Every member locks, so any number
        of threads may emit and consume concurrently.

        **Lifetime is the owner's problem, not this class's.** There is no registration, no
        subscriber list and no `weak_ptr` anywhere below — a `Signal` is an ordinary object with
        ordinary lifetime, and a sender holding a raw reference to a destroyed one is exactly as
        broken as it would be for any other object.

        One safe arrangement, should a sender ever outlive its receiver, is to hold the queue in a
        `std::shared_ptr` and give senders a `std::weak_ptr`: the receiver's destruction expires the
        weak pointer and a sender that locks it learns to stop, with no manual unregistration.
        `libs/signals/tests` demonstrates that. It is a **convention available to callers rather than
        a service this class provides**, and neither user in this repository needs it —
        `LoggingLib::Logger` and the renderer's `RenderChannel` both hold their queue as a plain
        member and outlive it by construction.
    */
    template <typename T> class Signal {
    public:
        //! Thread-safe enqueue.
        void emit(const T& data)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending.push(data);
        }

        //! Thread-safe dequeue; returns true if a value was consumed.
        [[nodiscard]] bool consume(T& out)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pending.empty()) {
                return false;
            }
            out = std::move(m_pending.front());
            m_pending.pop();
            return true;
        }

        /*!
            Thread-safe bulk dequeue: removes and returns every pending message in one lock acquisition.

            Equivalent to a `consume()` loop, with two differences a caller must want rather than merely tolerate. Messages
            emitted while the caller processes the batch wait for the next drain instead of extending this one, so a drain is
            bounded even if producers outpace the consumer. And `empty()` reports true from the moment of the swap, while the
            batch may still be unprocessed in the caller's hands — so a thread using emptiness as a proxy for "everything
            processed", as `Logger::flush` does, must stay with the `consume()` loop.
        */
        [[nodiscard]] std::queue<T> drain()
        {
            std::queue<T> all;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                all.swap(m_pending);
            }
            return all;
        }

        //! Returns true if the queue is empty.
        [[nodiscard]] bool empty() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending.empty();
        }

        //! Returns the number of pending messages.
        [[nodiscard]] std::size_t size() const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_pending.size();
        }

    private:
        std::queue<T> m_pending; //!< Queued messages.
        mutable std::mutex m_mutex; //!< Protects the queue.
    };

} // namespace SignalsLib
