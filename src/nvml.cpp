#include "nvml.h"

#include <stdarg.h>
#include <strsafe.h>

namespace {

constexpr int kNvmlSuccess = 0;
constexpr unsigned kNameBufferSize = 96;

template <typename T>
T LoadFunction(HMODULE library, const char* name) {
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(library, name)));
}

HMODULE LoadNvmlLibrary() {
    // Современные (DCH) драйверы кладут nvml.dll в System32.
    if (HMODULE library = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)) {
        return library;
    }
    // Старые драйверы ставят её в папку NVSMI.
    const wchar_t* candidates[] = {
        L"%ProgramW6432%\\NVIDIA Corporation\\NVSMI\\nvml.dll",
        L"%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll",
    };
    for (const wchar_t* candidate : candidates) {
        wchar_t path[MAX_PATH];
        DWORD length = ExpandEnvironmentStringsW(candidate, path, MAX_PATH);
        if (length == 0 || length > MAX_PATH) {
            continue;
        }
        if (HMODULE library = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
            return library;
        }
    }
    return nullptr;
}

}  // namespace

Nvml::~Nvml() {
    Shutdown();
}

bool Nvml::Init() {
    Shutdown();

    library_ = LoadNvmlLibrary();
    if (!library_) {
        SetError(L"nvml.dll не найдена: нужен драйвер NVIDIA");
        return false;
    }

    init_ = LoadFunction<InitFn>(library_, "nvmlInit_v2");
    shutdown_ = LoadFunction<ShutdownFn>(library_, "nvmlShutdown");
    getCount_ = LoadFunction<GetCountFn>(library_, "nvmlDeviceGetCount_v2");
    getHandle_ = LoadFunction<GetHandleFn>(library_, "nvmlDeviceGetHandleByIndex_v2");
    getName_ = LoadFunction<GetNameFn>(library_, "nvmlDeviceGetName");
    getMemory_ = LoadFunction<GetMemoryFn>(library_, "nvmlDeviceGetMemoryInfo");
    if (!init_ || !shutdown_ || !getCount_ || !getHandle_ || !getName_ || !getMemory_) {
        SetError(L"В nvml.dll нет нужных функций: обновите драйвер");
        Shutdown();
        return false;
    }

    int result = init_();
    if (result != kNvmlSuccess) {
        SetError(L"Не удалось инициализировать NVML (код %d)", result);
        Shutdown();
        return false;
    }

    ready_ = true;
    return true;
}

void Nvml::Shutdown() {
    if (ready_) {
        shutdown_();
        ready_ = false;
    }
    if (library_) {
        FreeLibrary(library_);
        library_ = nullptr;
    }
    init_ = nullptr;
    shutdown_ = nullptr;
    getCount_ = nullptr;
    getHandle_ = nullptr;
    getName_ = nullptr;
    getMemory_ = nullptr;
}

unsigned Nvml::DeviceCount() {
    unsigned count = 0;
    if (!ready_ || getCount_(&count) != kNvmlSuccess) {
        return 0;
    }
    return count;
}

bool Nvml::GetName(unsigned index, wchar_t* buffer, size_t length) {
    Handle device = nullptr;
    char name[kNameBufferSize] = "";
    if (!ready_ || getHandle_(index, &device) != kNvmlSuccess ||
        getName_(device, name, kNameBufferSize) != kNvmlSuccess) {
        return false;
    }
    return MultiByteToWideChar(CP_UTF8, 0, name, -1, buffer, static_cast<int>(length)) > 0;
}

bool Nvml::GetMemory(unsigned index, GpuMemory* out) {
    if (!ready_) {
        return false;
    }
    Handle device = nullptr;
    int result = getHandle_(index, &device);
    if (result == kNvmlSuccess) {
        result = getMemory_(device, out);
    }
    if (result != kNvmlSuccess) {
        SetError(L"Ошибка чтения памяти GPU %u (код %d)", index, result);
        return false;
    }
    return true;
}

void Nvml::SetError(const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    StringCchVPrintfW(error_, ARRAYSIZE(error_), format, args);
    va_end(args);
}
