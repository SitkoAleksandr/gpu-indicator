#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <strsafe.h>

#include "icon.h"
#include "nvml.h"
#include "resource.h"
#include "settings.h"

namespace {

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr UINT_PTR kTimerId = 1;
constexpr wchar_t kAppName[] = L"GPU Indicator";
constexpr wchar_t kWindowClass[] = L"GpuIndicatorWindow";
constexpr wchar_t kMutexName[] = L"Local\\GpuIndicator.SingleInstance";
constexpr DWORD kIntervals[] = {1000, 2000, 5000, 10000};
constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

constexpr COLORREF kGreen = RGB(46, 160, 67);
constexpr COLORREF kAmber = RGB(230, 166, 0);
constexpr COLORREF kRed = RGB(214, 40, 40);
constexpr COLORREF kGrey = RGB(110, 118, 129);
constexpr COLORREF kWhite = RGB(255, 255, 255);
constexpr COLORREF kDark = RGB(24, 24, 24);

enum Command : UINT {
    kCmdExit = 1,
    kCmdAutostart,
    kCmdModePercent,
    kCmdModeGigabytes,
    kCmdIntervalFirst = 100,
    kCmdGpuFirst = 200,
};

struct IconState {
    wchar_t text[8];
    COLORREF background;
    COLORREF foreground;
};

struct App {
    HWND window = nullptr;
    UINT taskbarCreatedMessage = 0;
    bool trayAdded = false;
    HICON icon = nullptr;
    IconState iconState = {};
    wchar_t tip[128] = L"";
    Settings settings;
    Nvml nvml;
};

App g_app;

unsigned SelectedGpu(unsigned count) {
    return g_app.settings.gpuIndex < count ? g_app.settings.gpuIndex : 0;
}

void SetUnknownState(IconState* state) {
    StringCchCopyW(state->text, ARRAYSIZE(state->text), L"?");
    state->background = kGrey;
    state->foreground = kWhite;
}

// Опрашивает NVML и решает, что показать на иконке и во всплывающей подсказке.
void ComputeState(IconState* state, wchar_t* tip, size_t tipLength) {
    if (!g_app.nvml.IsReady() && !g_app.nvml.Init()) {
        SetUnknownState(state);
        StringCchPrintfW(tip, tipLength, L"%s\n%s", kAppName, g_app.nvml.LastError());
        return;
    }

    unsigned count = g_app.nvml.DeviceCount();
    if (count == 0) {
        SetUnknownState(state);
        StringCchPrintfW(tip, tipLength, L"%s\nВидеокарта NVIDIA не найдена", kAppName);
        return;
    }

    unsigned index = SelectedGpu(count);
    GpuMemory memory = {};
    if (!g_app.nvml.GetMemory(index, &memory)) {
        SetUnknownState(state);
        StringCchPrintfW(tip, tipLength, L"%s\n%s", kAppName, g_app.nvml.LastError());
        // Например, после переустановки драйвера: на следующем тике переподключимся.
        g_app.nvml.Shutdown();
        return;
    }

    unsigned percent = memory.total ? static_cast<unsigned>((memory.used * 100 + memory.total / 2) / memory.total) : 0;
    double usedGb = memory.used / kGiB;
    double totalGb = memory.total / kGiB;
    double freeGb = memory.free / kGiB;

    if (g_app.settings.mode == DisplayMode::Percent) {
        StringCchPrintfW(state->text, ARRAYSIZE(state->text), L"%u", percent);
    } else if (usedGb < 9.95) {
        StringCchPrintfW(state->text, ARRAYSIZE(state->text), L"%.1f", usedGb);
    } else {
        StringCchPrintfW(state->text, ARRAYSIZE(state->text), L"%.0f", usedGb);
    }

    if (percent >= 90) {
        state->background = kRed;
        state->foreground = kWhite;
    } else if (percent >= 70) {
        state->background = kAmber;
        state->foreground = kDark;
    } else {
        state->background = kGreen;
        state->foreground = kWhite;
    }

    wchar_t name[96];
    if (!g_app.nvml.GetName(index, name, ARRAYSIZE(name))) {
        StringCchCopyW(name, ARRAYSIZE(name), L"NVIDIA GPU");
    }
    StringCchPrintfW(tip, tipLength, L"%s\nVRAM: %.2f / %.2f ГБ (%u%%)\nСвободно: %.2f ГБ", name, usedGb, totalGb,
                     percent, freeGb);
}

NOTIFYICONDATAW MakeNotifyData(UINT flags) {
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = g_app.window;
    data.uID = kTrayId;
    data.uFlags = flags;
    data.uCallbackMessage = WM_TRAYICON;
    data.hIcon = g_app.icon;
    StringCchCopyW(data.szTip, ARRAYSIZE(data.szTip), g_app.tip);
    return data;
}

bool ModifyTrayIcon() {
    NOTIFYICONDATAW data = MakeNotifyData(NIF_ICON | NIF_TIP | NIF_SHOWTIP);
    return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

bool AddTrayIcon() {
    NOTIFYICONDATAW data = MakeNotifyData(NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP);
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        // Иконка могла остаться с прошлого раза (например, после смены DPI).
        return ModifyTrayIcon();
    }
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
    return true;
}

void RemoveTrayIcon() {
    if (g_app.trayAdded) {
        NOTIFYICONDATAW data = MakeNotifyData(0);
        Shell_NotifyIconW(NIM_DELETE, &data);
        g_app.trayAdded = false;
    }
}

// force: перерисовать иконку, даже если значение не изменилось (смена DPI, настроек).
void Refresh(bool force) {
    IconState state = {};
    wchar_t tip[ARRAYSIZE(g_app.tip)];
    ComputeState(&state, tip, ARRAYSIZE(tip));

    bool iconChanged = force || !g_app.icon || wcscmp(state.text, g_app.iconState.text) != 0 ||
                       state.background != g_app.iconState.background ||
                       state.foreground != g_app.iconState.foreground;
    bool tipChanged = wcscmp(tip, g_app.tip) != 0;
    if (!iconChanged && !tipChanged && g_app.trayAdded) {
        return;
    }

    HICON oldIcon = nullptr;
    if (iconChanged) {
        HICON icon = CreateIndicatorIcon(GetSystemMetrics(SM_CXSMICON), state.text, state.background,
                                         state.foreground);
        if (icon) {
            oldIcon = g_app.icon;
            g_app.icon = icon;
            g_app.iconState = state;
        }
    }
    StringCchCopyW(g_app.tip, ARRAYSIZE(g_app.tip), tip);

    // При автозапуске Explorer может быть ещё не готов: тогда пробуем снова на следующем тике.
    if (g_app.trayAdded) {
        g_app.trayAdded = ModifyTrayIcon();
    }
    if (!g_app.trayAdded) {
        g_app.trayAdded = AddTrayIcon();
    }

    // Shell хранит свою копию иконки, так что старую можно удалить после NIM_MODIFY.
    if (oldIcon) {
        DestroyIcon(oldIcon);
    }
}

void ShowMenu(POINT point) {
    HMENU menu = CreatePopupMenu();

    unsigned count = g_app.nvml.IsReady() ? g_app.nvml.DeviceCount() : 0;
    if (count > 1) {
        HMENU gpus = CreatePopupMenu();
        unsigned selected = SelectedGpu(count);
        for (unsigned i = 0; i < count; ++i) {
            wchar_t name[96];
            if (!g_app.nvml.GetName(i, name, ARRAYSIZE(name))) {
                StringCchCopyW(name, ARRAYSIZE(name), L"NVIDIA GPU");
            }
            wchar_t label[112];
            StringCchPrintfW(label, ARRAYSIZE(label), L"%u: %s", i, name);
            AppendMenuW(gpus, MF_STRING | (i == selected ? MF_CHECKED : 0), kCmdGpuFirst + i, label);
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gpus), L"Видеокарта");
    }

    HMENU mode = CreatePopupMenu();
    bool percent = g_app.settings.mode == DisplayMode::Percent;
    AppendMenuW(mode, MF_STRING | (percent ? MF_CHECKED : 0), kCmdModePercent, L"Процент занятой памяти");
    AppendMenuW(mode, MF_STRING | (percent ? 0 : MF_CHECKED), kCmdModeGigabytes, L"Занято, ГБ");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(mode), L"Показывать на иконке");

    HMENU interval = CreatePopupMenu();
    for (UINT i = 0; i < ARRAYSIZE(kIntervals); ++i) {
        wchar_t label[16];
        StringCchPrintfW(label, ARRAYSIZE(label), L"%u с", kIntervals[i] / 1000);
        UINT checked = kIntervals[i] == g_app.settings.intervalMs ? MF_CHECKED : 0;
        AppendMenuW(interval, MF_STRING | checked, kCmdIntervalFirst + i, label);
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(interval), L"Частота обновления");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kCmdAutostart,
                L"Запускать вместе с Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"Выход");

    // Без SetForegroundWindow меню не закрывается по клику мимо, без WM_NULL — открывается через раз.
    SetForegroundWindow(g_app.window);
    UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    UINT command = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, point.x, point.y,
        g_app.window, nullptr));
    PostMessageW(g_app.window, WM_NULL, 0, 0);
    DestroyMenu(menu);  // подменю удаляются вместе с ним

    if (command == kCmdExit) {
        DestroyWindow(g_app.window);
    } else if (command == kCmdAutostart) {
        if (!SetAutostart(!IsAutostartEnabled())) {
            MessageBoxW(nullptr, L"Не удалось изменить настройку автозапуска.", kAppName, MB_ICONWARNING);
        }
    } else if (command == kCmdModePercent || command == kCmdModeGigabytes) {
        g_app.settings.mode = command == kCmdModePercent ? DisplayMode::Percent : DisplayMode::Gigabytes;
        SaveSettings(g_app.settings);
        Refresh(true);
    } else if (command >= kCmdIntervalFirst && command < kCmdIntervalFirst + ARRAYSIZE(kIntervals)) {
        g_app.settings.intervalMs = kIntervals[command - kCmdIntervalFirst];
        SaveSettings(g_app.settings);
        SetTimer(g_app.window, kTimerId, g_app.settings.intervalMs, nullptr);
    } else if (command >= kCmdGpuFirst && command < kCmdGpuFirst + count) {
        g_app.settings.gpuIndex = command - kCmdGpuFirst;
        SaveSettings(g_app.settings);
        Refresh(true);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Explorer перезапустился (или сменился DPI): иконку нужно добавить заново.
    if (message == g_app.taskbarCreatedMessage && message != 0) {
        g_app.trayAdded = false;
        Refresh(true);
        return 0;
    }

    switch (message) {
        case WM_TIMER:
            if (wParam == kTimerId) {
                Refresh(false);
            }
            return 0;

        case WM_TRAYICON:
            // NOTIFYICON_VERSION_4: событие в LOWORD(lParam), координаты курсора в wParam.
            switch (LOWORD(lParam)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    ShowMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                    break;
            }
            return 0;

        case WM_DESTROY:
            KillTimer(window, kTimerId);
            RemoveTrayIcon();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;  // уже запущены
    }

    g_app.settings = LoadSettings();
    g_app.taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        return 1;
    }

    // Обычное скрытое окно, а не message-only: тем не приходит широковещательное TaskbarCreated.
    g_app.window = CreateWindowExW(0, kWindowClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                   instance, nullptr);
    if (!g_app.window) {
        return 1;
    }
    if (g_app.taskbarCreatedMessage) {
        ChangeWindowMessageFilterEx(g_app.window, g_app.taskbarCreatedMessage, MSGFLT_ALLOW, nullptr);
    }

    Refresh(true);
    SetTimer(g_app.window, kTimerId, g_app.settings.intervalMs, nullptr);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_app.icon) {
        DestroyIcon(g_app.icon);
    }
    g_app.nvml.Shutdown();
    if (mutex) {
        CloseHandle(mutex);
    }
    return 0;
}
