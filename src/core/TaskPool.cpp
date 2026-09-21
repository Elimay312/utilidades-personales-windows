#include "core/TaskPool.h"

#include <algorithm>
#include <utility>

TaskPool::~TaskPool() {
    Stop();
}

void TaskPool::Start(unsigned threads) {
    const unsigned count = std::clamp(threads, 2u, 4u);
    m_threads.reserve(count);
    for (unsigned i = 0; i < count; ++i)
        m_threads.emplace_back([this] { Worker(); });
}

void TaskPool::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) return;
        m_stopping = true;
    }
    m_wake.notify_all();
    m_threads.clear();  // destruir un jthread es unirlo
}

void TaskPool::Submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) return;
        m_tasks.push_back(std::move(task));
    }
    m_wake.notify_one();
}

void TaskPool::Worker() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
            if (m_tasks.empty()) return;  // solo se sale con la cola vacia y m_stopping
            task = std::move(m_tasks.front());
            m_tasks.pop_front();
        }
        task();
    }
}
