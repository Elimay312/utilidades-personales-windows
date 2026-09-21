#include "core/Diag.h"

#include <Windows.h>

#include <cstdio>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace Diag {
namespace {

struct Stage {
    const char* name;
    long long tick;
};

// Sin cerrojo: esto solo lo toca el hilo de UI, al arrancar y al cerrar.
constexpr int kMaxStages = 12;
Stage g_stages[kMaxStages];
int g_count = 0;
long long g_frequency = 0;
double g_loaderMs = 0.0;  // del arranque del proceso al primer Mark
HANDLE g_file = INVALID_HANDLE_VALUE;

unsigned long long AsU64(const FILETIME& time) {
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

double MsBetween(long long from, long long to) {
    if (g_frequency == 0) return 0.0;
    return static_cast<double>(to - from) * 1000.0 / static_cast<double>(g_frequency);
}

}  // namespace

void Mark(const char* stage) {
    if (g_count == 0) {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        g_frequency = frequency.QuadPart;

        // El cargador y los constructores estaticos pasan antes de wWinMain: sin esto el
        // "arranque en frio" se mediria desde la mitad.
        FILETIME creation{}, exited{}, kernel{}, user{}, now{};
        if (GetProcessTimes(GetCurrentProcess(), &creation, &exited, &kernel, &user)) {
            GetSystemTimeAsFileTime(&now);
            g_loaderMs = static_cast<double>(AsU64(now) - AsU64(creation)) / 10000.0;
        }
    }
    if (g_count >= kMaxStages) return;

    LARGE_INTEGER tick{};
    QueryPerformanceCounter(&tick);
    g_stages[g_count++] = Stage{stage, tick.QuadPart};
}

void Open(const std::wstring& path) {
    constexpr DWORD kShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, kShare, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);

    // Una linea por ejecucion no crece rapido, pero tampoco puede crecer para siempre.
    LARGE_INTEGER size{};
    if (g_file != INVALID_HANDLE_VALUE && GetFileSizeEx(g_file, &size) &&
        size.QuadPart > 64 * 1024) {
        CloseHandle(g_file);
        g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, kShare, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    }

#ifdef _DEBUG
    // El informe de fugas sale al final del proceso, cuando ya no hay nadie escuchando:
    // mandarlo a este archivo es la unica forma de verlo sin depurador.
    if (g_file != INVALID_HANDLE_VALUE) {
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, g_file);
        _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_ALLOC_MEM_DF |
                       _CRTDBG_LEAK_CHECK_DF);
    }
#endif
}

void Log(const std::string& text) {
    if (g_file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    char stamp[32];
    snprintf(stamp, sizeof(stamp), "%04u-%02u-%02u %02u:%02u:%02u  ", now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);

    const std::string line = stamp + text + "\r\n";
    DWORD written = 0;
    WriteFile(g_file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

void WriteStartup() {
    if (g_count == 0) return;

    char piece[128];
    snprintf(piece, sizeof(piece), "arranque: proceso->%s %.1f", g_stages[0].name, g_loaderMs);
    std::string line = piece;
    for (int i = 1; i < g_count; ++i) {
        snprintf(piece, sizeof(piece), " | %s %.1f", g_stages[i].name,
                 MsBetween(g_stages[i - 1].tick, g_stages[i].tick));
        line += piece;
    }
    snprintf(piece, sizeof(piece), " | TOTAL %.1f ms",
             g_loaderMs + MsBetween(g_stages[0].tick, g_stages[g_count - 1].tick));
    Log(line + piece);
}

}  // namespace Diag
