#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Pool pequeno de hilos para el trabajo de disco. En reposo todos duermen en el wait:
// cero CPU. El hilo de UI solo hace Submit, nunca espera aqui.
class TaskPool {
public:
    ~TaskPool();

    void Start(unsigned threads);  // limitado a [2, 4]
    void Stop();
    void Submit(std::function<void()> task);

private:
    void Worker();

    std::vector<std::jthread> m_threads;
    std::deque<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopping = false;
};
