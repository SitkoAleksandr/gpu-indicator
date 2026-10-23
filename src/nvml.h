#pragma once

#include <windows.h>

struct GpuMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

// Минимальная обёртка над NVML (nvml.dll из драйвера NVIDIA).
// Библиотека загружается динамически, поэтому CUDA SDK для сборки не нужен,
// а без драйвера NVIDIA программа просто сообщает об ошибке.
class Nvml {
public:
    Nvml() = default;
    ~Nvml();
    Nvml(const Nvml&) = delete;
    Nvml& operator=(const Nvml&) = delete;

    bool Init();
    void Shutdown();
    bool IsReady() const { return ready_; }

    unsigned DeviceCount();
    bool GetName(unsigned index, wchar_t* buffer, size_t length);
    bool GetMemory(unsigned index, GpuMemory* out);

    const wchar_t* LastError() const { return error_; }

private:
    struct Device;
    using Handle = Device*;
    using InitFn = int(__cdecl*)();
    using ShutdownFn = int(__cdecl*)();
    using GetCountFn = int(__cdecl*)(unsigned*);
    using GetHandleFn = int(__cdecl*)(unsigned, Handle*);
    using GetNameFn = int(__cdecl*)(Handle, char*, unsigned);
    using GetMemoryFn = int(__cdecl*)(Handle, GpuMemory*);

    void SetError(const wchar_t* format, ...);

    HMODULE library_ = nullptr;
    bool ready_ = false;
    InitFn init_ = nullptr;
    ShutdownFn shutdown_ = nullptr;
    GetCountFn getCount_ = nullptr;
    GetHandleFn getHandle_ = nullptr;
    GetNameFn getName_ = nullptr;
    GetMemoryFn getMemory_ = nullptr;
    wchar_t error_[160] = L"";
};
