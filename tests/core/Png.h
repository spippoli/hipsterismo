#pragma once

// A minimal PNG reader for the core tests: 8-bit grayscale, RGB or RGBA,
// non-interlaced, which covers tests/fixtures/. Decoded with zlib, no
// openFrameworks and no OpenCV imgcodecs, so it also builds with the NDK.

#include <cstdint>
#include <string>
#include <vector>

struct RgbPixels {
    int width = 0, height = 0;
    std::vector<uint8_t> data; // tightly packed RGB, row by row
};

// Loads `path` into `pixels`; on failure returns false and sets `error`.
bool loadPng(const std::string& path, RgbPixels& pixels, std::string& error);
