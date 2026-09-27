#pragma once

// El hilo para todo lo que puede bloquear: esperar al proceso que extrae los iconos ahora,
// listas de saltos y Steam después. Los trabajos se ejecutan en orden y avisan a la ventana
// con PostMessage; el hilo de UI no espera nunca a uno. Copiado de Panel, con COM en STA en
// vez de MTA: las APIs del shell que vendrán son de apartamento.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace dock {

class Worker {
 public:
  Worker() = default;
  ~Worker() { Stop(); }
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void Start();
  // Ejecuta lo que quede en cola y termina el hilo.
  void Stop();
  void Post(std::function<void()> job);

 private:
  void Loop();

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_ = false;
};

}  // namespace dock
