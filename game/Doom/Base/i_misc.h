#pragma once

#include <cstdint>

// Definition for a big font character: where it is on the 'STATUS' texture atlas and it's size
struct fontchar_t {
    std::uint8_t u;
    std::uint8_t v;
    std::uint8_t w;
    std::uint8_t h;
};

// The list of big font characters
static constexpr int32_t NUM_BIG_FONT_CHARS = 66;
extern const fontchar_t gBigFontChars[NUM_BIG_FONT_CHARS];

// Starting indices for various individual and groups of big font chars
enum : int32_t {
    BIG_FONT_DIGITS         = 0,
    BIG_FONT_MINUS          = 10,
    BIG_FONT_PERCENT        = 11,
    BIG_FONT_EXCLAMATION    = 12,
    BIG_FONT_PERIOD         = 13,
    BIG_FONT_UCASE_ALPHA    = 14,
    BIG_FONT_LCASE_ALPHA    = 40
};

void I_DrawNumber(const int32_t x, const int32_t y, const int32_t value) noexcept;
void I_DrawStringSmall(const int32_t x, const int32_t y, const char* const str) noexcept;
void I_DrawPausedOverlay() noexcept;
void I_UpdatePalette() noexcept;
void I_DrawString(const int32_t x, const int32_t y, const char* const str) noexcept;
