#include "Png.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include <zlib.h>

namespace {

uint32_t bigEndian(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return uint8_t(a);
    return uint8_t(pb <= pc ? b : c);
}

} // namespace

bool loadPng(const std::string& path, RgbPixels& pixels, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), signature, 8) != 0) {
        error = path + " is not a PNG";
        return false;
    }

    int width = 0, height = 0, channels = 0;
    std::vector<uint8_t> compressed;
    for (size_t at = 8; at + 12 <= bytes.size();) {
        const uint32_t length = bigEndian(&bytes[at]);
        if (at + 12 + length > bytes.size()) break;
        const std::string type(reinterpret_cast<const char*>(&bytes[at + 4]), 4);
        const uint8_t* body = &bytes[at + 8];
        if (type == "IHDR") {
            width = int(bigEndian(body));
            height = int(bigEndian(body + 4));
            const int depth = body[8], colour = body[9], interlace = body[12];
            channels = colour == 0 ? 1 : colour == 2 ? 3 : colour == 6 ? 4 : 0;
            if (depth != 8 || channels == 0 || interlace != 0) {
                error = path + ": only 8-bit gray, RGB or RGBA non-interlaced PNGs are supported";
                return false;
            }
        } else if (type == "IDAT") {
            compressed.insert(compressed.end(), body, body + length);
        } else if (type == "IEND") {
            break;
        }
        at += 12 + length;
    }
    if (width <= 0 || height <= 0 || compressed.empty()) {
        error = path + ": no image data";
        return false;
    }

    const size_t rowBytes = size_t(width) * channels;
    std::vector<uint8_t> raw((rowBytes + 1) * height);
    uLongf rawSize = uLongf(raw.size());
    if (uncompress(raw.data(), &rawSize, compressed.data(), uLong(compressed.size())) != Z_OK
        || rawSize != raw.size()) {
        error = path + ": corrupt image data";
        return false;
    }

    // Undo the per-row filters in place, then keep the RGB channels.
    std::vector<uint8_t> previous(rowBytes, 0), row(rowBytes);
    pixels.width = width;
    pixels.height = height;
    pixels.data.assign(size_t(width) * height * 3, 0);
    for (int y = 0; y < height; y++) {
        const uint8_t filter = raw[y * (rowBytes + 1)];
        const uint8_t* in = &raw[y * (rowBytes + 1) + 1];
        for (size_t i = 0; i < rowBytes; i++) {
            const int a = i >= size_t(channels) ? row[i - channels] : 0, b = previous[i];
            const int c = i >= size_t(channels) ? previous[i - channels] : 0;
            int value = in[i];
            switch (filter) {
            case 0: break;
            case 1: value += a; break;
            case 2: value += b; break;
            case 3: value += (a + b) / 2; break;
            case 4: value += paeth(a, b, c); break;
            default: error = path + ": bad row filter"; return false;
            }
            row[i] = uint8_t(value);
        }
        for (int x = 0; x < width; x++) {
            uint8_t* out = &pixels.data[(size_t(y) * width + x) * 3];
            const uint8_t* p = &row[size_t(x) * channels];
            out[0] = p[0];
            out[1] = channels >= 3 ? p[1] : p[0];
            out[2] = channels >= 3 ? p[2] : p[0];
        }
        std::swap(previous, row);
    }
    return true;
}
