#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Vigila unas pocas carpetas con ReadDirectoryChangesW en modo overlapped desde un hilo
// propio. No dice QUE cambio, solo la ruta de la carpeta: la respuesta siempre es releerla
// entera, asi que descodificar FILE_NOTIFY_INFORMATION no aportaria nada.
//
// onChanged se llama DESDE EL HILO DEL VIGILANTE y puede llegar en rafagas (copiar mil
// archivos son mil avisos): quien lo reciba tiene que ser seguro para hilos y agrupar.
class DirectoryWatcher {
public:
    // Los dos en el .cpp: Entry es incompleto aqui y el vector de unique_ptr necesita verlo
    // para destruirse.
    DirectoryWatcher();
    ~DirectoryWatcher();

    void Start(std::function<void(const std::wstring&)> onChanged);
    void Stop();  // cancela todo y hace join; llamarlo dos veces no hace nada

    // Sustituye el conjunto vigilado. Lo que ya estaba no se reabre y lo que sobra se
    // cancela. Se llama desde el hilo de UI y no bloquea: el trabajo viaja por una APC.
    // La ruta vacia (raiz virtual) se ignora.
    void Watch(std::vector<std::wstring> paths);

private:
    struct Entry;

    static void CALLBACK ApplyApc(ULONG_PTR param);
    static void CALLBACK StopApc(ULONG_PTR param);
    static void CALLBACK Completion(DWORD error, DWORD bytes, LPOVERLAPPED overlapped);

    // De aqui abajo todo corre SOLO en el hilo del vigilante (dentro de APCs y de rutinas de
    // terminacion, que se ejecutan en ese hilo y de una en una): ni un cerrojo.
    void Apply(const std::vector<std::wstring>& paths);
    void Open(const std::wstring& path);
    bool Issue(Entry* entry);
    void Abandon(size_t index);
    void Destroy(Entry* entry);

    std::function<void(const std::wstring&)> m_onChanged;
    std::vector<std::unique_ptr<Entry>> m_entries;
    // Entry vivos, incluidos los ya cancelados que aun esperan su ERROR_OPERATION_ABORTED.
    // Stop no puede salir mientras quede uno: cerrar su handle antes seria una carrera.
    size_t m_alive = 0;
    bool m_quit = false;
    std::thread m_thread;
};
