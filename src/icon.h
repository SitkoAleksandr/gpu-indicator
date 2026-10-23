#pragma once

#include <windows.h>

// Рисует квадратную иконку size x size: цветная плашка со скруглёнными углами
// и текст по центру. Размер шрифта подбирается так, чтобы текст влез.
HICON CreateIndicatorIcon(int size, const wchar_t* text, COLORREF background, COLORREF foreground);
