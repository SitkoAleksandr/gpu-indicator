#include "settings.h"

#include <strsafe.h>

namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\GpuIndicator";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"GpuIndicator";

DWORD ReadDword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return fallback;
    }
    return value;
}

void WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
}

// Путь к exe в кавычках, в том виде, в каком он пишется в Run.
bool GetRunCommand(wchar_t* buffer, size_t length) {
    wchar_t path[MAX_PATH];
    DWORD pathLength = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (pathLength == 0 || pathLength >= MAX_PATH) {
        return false;
    }
    return SUCCEEDED(StringCchPrintfW(buffer, length, L"\"%s\"", path));
}

}  // namespace

Settings LoadSettings() {
    Settings settings;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        settings.gpuIndex = ReadDword(key, L"GpuIndex", settings.gpuIndex);
        settings.intervalMs = ReadDword(key, L"IntervalMs", settings.intervalMs);
        settings.mode = static_cast<DisplayMode>(ReadDword(key, L"Mode", static_cast<DWORD>(settings.mode)));
        RegCloseKey(key);
    }

    Settings defaults;
    if (settings.intervalMs < 500 || settings.intervalMs > 60000) {
        settings.intervalMs = defaults.intervalMs;
    }
    if (settings.mode != DisplayMode::Percent && settings.mode != DisplayMode::Gigabytes) {
        settings.mode = defaults.mode;
    }
    return settings;
}

void SaveSettings(const Settings& settings) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return;
    }
    WriteDword(key, L"GpuIndex", settings.gpuIndex);
    WriteDword(key, L"IntervalMs", settings.intervalMs);
    WriteDword(key, L"Mode", static_cast<DWORD>(settings.mode));
    RegCloseKey(key);
}

bool IsAutostartEnabled() {
    wchar_t expected[MAX_PATH + 3];
    if (!GetRunCommand(expected, ARRAYSIZE(expected))) {
        return false;
    }
    wchar_t value[MAX_PATH + 3];
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) {
        return false;
    }
    // Если exe перенесли, старая запись указывает не туда — считаем автозапуск выключенным.
    return CompareStringOrdinal(value, -1, expected, -1, TRUE) == CSTR_EQUAL;
}

bool SetAutostart(bool enable) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status;
    if (enable) {
        wchar_t command[MAX_PATH + 3];
        if (!GetRunCommand(command, ARRAYSIZE(command))) {
            RegCloseKey(key);
            return false;
        }
        DWORD bytes = static_cast<DWORD>((wcslen(command) + 1) * sizeof(wchar_t));
        status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command), bytes);
    } else {
        status = RegDeleteValueW(key, kRunValue);
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
