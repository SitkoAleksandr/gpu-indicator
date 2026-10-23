#include "icon.h"

#include <math.h>
#include <string.h>

#include <vector>

namespace {

struct Box {
    int left = 0;
    int top = 0;
    int right = 0;  // не включительно
    int bottom = 0;

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
};

// Холст для текста: GDI рисует белым по чёрному, яркость пикселя = покрытие глифом.
// Холст вдвое больше иконки, чтобы крупный шрифт при подборе не обрезался.
class TextCanvas {
public:
    explicit TextCanvas(int side) : side_(side) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = side;
        info.bmiHeader.biHeight = -side;  // строки сверху вниз
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        dc_ = CreateCompatibleDC(nullptr);
        bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&bits_), nullptr, 0);
        if (bitmap_) {
            oldBitmap_ = SelectObject(dc_, bitmap_);
            SetBkMode(dc_, TRANSPARENT);
            SetTextColor(dc_, RGB(255, 255, 255));
        }
    }

    ~TextCanvas() {
        if (oldBitmap_) {
            SelectObject(dc_, oldBitmap_);
        }
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
        if (dc_) {
            DeleteDC(dc_);
        }
    }

    bool IsValid() const { return bits_ != nullptr; }

    // Рисует текст шрифтом высотой fontPx и возвращает рамку закрашенных пикселей.
    Box Render(const wchar_t* text, int fontPx) {
        memset(bits_, 0, static_cast<size_t>(side_) * side_ * sizeof(*bits_));

        HFONT font = CreateFontW(-fontPx, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc_, font);
        TextOutW(dc_, 1, 1, text, static_cast<int>(wcslen(text)));
        SelectObject(dc_, oldFont);
        DeleteObject(font);
        GdiFlush();

        Box box = {side_, side_, 0, 0};
        for (int y = 0; y < side_; ++y) {
            for (int x = 0; x < side_; ++x) {
                if (Coverage(x, y) > 0) {
                    if (x < box.left) box.left = x;
                    if (y < box.top) box.top = y;
                    if (x >= box.right) box.right = x + 1;
                    if (y >= box.bottom) box.bottom = y + 1;
                }
            }
        }
        if (box.right == 0) {
            box = {};
        }
        return box;
    }

    // Покрытие глифом в точке (0..255), вне холста — 0.
    int Coverage(int x, int y) const {
        if (x < 0 || y < 0 || x >= side_ || y >= side_) {
            return 0;
        }
        return (bits_[y * side_ + x] >> 8) & 0xFF;  // зелёный канал
    }

private:
    int side_;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    unsigned* bits_ = nullptr;
};

// Подбирать шрифт по «худшему» тексту той же длины (цифры -> '8', не короче двух
// символов), чтобы размер цифр не скакал при смене значения, например 9% -> 10%.
void MakeReferenceText(const wchar_t* text, wchar_t* out, size_t length) {
    size_t i = 0;
    if (wcslen(text) < 2 && i + 1 < length) {
        out[i++] = L'8';
    }
    for (; *text && i + 1 < length; ++text) {
        out[i++] = (*text >= L'0' && *text <= L'9') ? L'8' : *text;
    }
    out[i] = L'\0';
}

int PickFontSize(TextCanvas& canvas, const wchar_t* text, int size) {
    wchar_t reference[16];
    MakeReferenceText(text, reference, ARRAYSIZE(reference));
    int padding = size >= 24 ? 2 : 1;
    int limit = size - 2 * padding;
    for (int fontPx = size + size / 2; fontPx > 4; --fontPx) {
        Box box = canvas.Render(reference, fontPx);
        if (box.Width() <= limit && box.Height() <= limit) {
            return fontPx;
        }
    }
    return 5;
}

// Покрытие плашки со скруглёнными углами в пикселе (x, y), со сглаживанием по краю.
float BackgroundCoverage(int x, int y, int size) {
    float radius = size * 0.22f;
    float px = x + 0.5f;
    float py = y + 0.5f;
    float dx = 0.0f;
    float dy = 0.0f;
    if (px < radius) dx = radius - px;
    if (px > size - radius) dx = px - (size - radius);
    if (py < radius) dy = radius - py;
    if (py > size - radius) dy = py - (size - radius);
    float coverage = radius - sqrtf(dx * dx + dy * dy) + 0.5f;
    return coverage < 0.0f ? 0.0f : (coverage > 1.0f ? 1.0f : coverage);
}

unsigned char Mix(unsigned char from, unsigned char to, float t) {
    return static_cast<unsigned char>(from + (to - from) * t + 0.5f);
}

}  // namespace

HICON CreateIndicatorIcon(int size, const wchar_t* text, COLORREF background, COLORREF foreground) {
    TextCanvas canvas(size * 2);
    if (!canvas.IsValid()) {
        return nullptr;
    }

    Box box = canvas.Render(text, PickFontSize(canvas, text, size));
    int offsetX = (size - box.Width()) / 2 - box.left;
    int offsetY = (size - box.Height()) / 2 - box.top;

    // 32-битный цвет с прямым (не premultiplied) альфа-каналом, формат BGRA.
    std::vector<unsigned> pixels(static_cast<size_t>(size) * size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float bg = BackgroundCoverage(x, y, size);
            float t = canvas.Coverage(x - offsetX, y - offsetY) / 255.0f;
            float alpha = bg > t ? bg : t;
            unsigned r = Mix(GetRValue(background), GetRValue(foreground), t);
            unsigned g = Mix(GetGValue(background), GetGValue(foreground), t);
            unsigned b = Mix(GetBValue(background), GetBValue(foreground), t);
            unsigned a = static_cast<unsigned>(alpha * 255.0f + 0.5f);
            pixels[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* colorBits = nullptr;
    HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &colorBits, nullptr, 0);
    if (!color) {
        return nullptr;
    }
    memcpy(colorBits, pixels.data(), pixels.size() * sizeof(pixels[0]));

    // Маска обязательна, но при 32-битном цвете с альфой Windows её не использует.
    std::vector<unsigned char> maskBits(static_cast<size_t>((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = color;
    iconInfo.hbmMask = mask;
    HICON icon = mask ? CreateIconIndirect(&iconInfo) : nullptr;

    DeleteObject(color);
    if (mask) {
        DeleteObject(mask);
    }
    return icon;
}
