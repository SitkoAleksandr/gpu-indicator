#pragma once

#include <windows.h>

enum class DisplayMode : DWORD {
    Percent = 0,    // процент занятой VRAM
    Gigabytes = 1,  // занято, ГБ
};

struct Settings {
    DWORD gpuIndex = 0;
    DWORD intervalMs = 2000;
    DisplayMode mode = DisplayMode::Percent;
};

// Настройки хранятся в HKCU\Software\GpuIndicator, чтобы рядом с exe не было лишних файлов.
Settings LoadSettings();
void SaveSettings(const Settings& settings);

// Автозапуск через HKCU\...\CurrentVersion\Run, права администратора не нужны.
bool IsAutostartEnabled();
bool SetAutostart(bool enable);
