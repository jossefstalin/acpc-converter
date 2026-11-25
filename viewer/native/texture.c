#include "viewer.h"

static uint32_t argb(unsigned r, unsigned g, unsigned b, unsigned a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static uint32_t color565(uint32_t color) {
    unsigned r = color >> 11, g = (color >> 5) & 63u, b = color & 31u;
    return argb((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
}

static uint32_t color_mix(uint32_t a, uint32_t b, unsigned first, unsigned second) {
    const unsigned total = first + second;
    uint32_t mixed = 0xff000000U;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned ca = (a >> shift) & 255u, cb = (b >> shift) & 255u;
        mixed |= ((ca * first + cb * second) / total) << shift;
    }
    return mixed;
}

static unsigned mask_channel(uint32_t raw, uint32_t mask, unsigned fallback) {
    if (!mask) return fallback;
    unsigned shift = 0;
    while (!(mask & 1u)) { mask >>= 1; ++shift; }
    return (unsigned)(((uint64_t)((raw >> shift) & mask) * 255u + mask / 2) / mask);
}

static int allocate_image(AvImage *image, uint32_t width, uint32_t height) {
    if (!width || !height || width > 8192 || height > 8192 || (uint64_t)width * height > 16777216) return 0;
    uint32_t *pixels = malloc((size_t)width * height * sizeof(uint32_t));
    if (!pixels) return 0;
    *image = (AvImage){(int)width, (int)height, pixels};
    return 1;
}

static void block_alpha(const unsigned char *block, int dxt, unsigned alpha[16]) {
    for (unsigned i = 0; i < 16; ++i) alpha[i] = 255;
    if (dxt == 3) {
        for (unsigned i = 0; i < 16; ++i) alpha[i] = ((block[i/2] >> (4*(i%2))) & 15u) * 17u;
    }
    if (dxt == 5) {
        unsigned a[8] = {block[0], block[1], 0, 0, 0, 0, 0, 255};
        if (a[0] > a[1]) {
            for (unsigned i = 1; i <= 6; ++i) a[i+1] = ((7-i)*a[0] + i*a[1])/7;
        } else {
            for (unsigned i = 1; i <= 4; ++i) a[i+1] = ((5-i)*a[0] + i*a[1])/5;
        }
        uint64_t indices = 0;
        for (unsigned i = 0; i < 6; ++i) indices |= (uint64_t)block[2+i] << (8*i);
        for (unsigned i = 0; i < 16; ++i) alpha[i] = a[(indices >> (3*i)) & 7u];
    }
}

int av_dds(const unsigned char *data, size_t size, AvImage *image) {
    if (size < 128 || memcmp(data, "DDS ", 4) || av_u32(data + 4) != 124 || av_u32(data + 76) != 32) return 0;
    uint32_t height = av_u32(data + 12), width = av_u32(data + 16);
    if (!width || !height || width > 8192 || height > 8192 || (uint64_t)width * height > 16777216) return 0;
    uint32_t flags = av_u32(data + 80);
    if (flags & 0x40) {
        uint32_t bits = av_u32(data + 88);
        if (bits != 24 && bits != 32) return 0;
        size_t pitch = (size_t)width * (bits / 8);
        uint32_t declared_pitch = av_u32(data + 20);
        if ((av_u32(data + 8) & 8) && declared_pitch >= pitch && (uint64_t)declared_pitch * height <= size - 128) {
            pitch = declared_pitch;
        }
        if (pitch * height > size - 128 || !allocate_image(image, width, height)) return 0;
        uint32_t red = av_u32(data + 92), green = av_u32(data + 96), blue = av_u32(data + 100), alpha = av_u32(data + 104);
        for (uint32_t y = 0; y < height; ++y) {
            const unsigned char *row = data + 128 + (size_t)y * pitch;
            for (uint32_t x = 0; x < width; ++x, row += bits / 8) {
                uint32_t pixel = bits == 32 ? av_u32(row) : av_u16(row) | ((uint32_t)row[2] << 16);
                image->pixels[(size_t)y * width + x] = argb(mask_channel(pixel, red, 0), mask_channel(pixel, green, 0),
                    mask_channel(pixel, blue, 0), mask_channel(pixel, alpha, 255));
            }
        }
        return 1;
    }
    if (!(flags & 4)) return 0;
    int dxt = memcmp(data + 84, "DXT1", 4) == 0 ? 1 : memcmp(data + 84, "DXT3", 4) == 0 ? 3
        : memcmp(data + 84, "DXT5", 4) == 0 ? 5 : 0;
    if (!dxt) return 0;
    size_t bytes = dxt == 1 ? 8 : 16;
    if ((size_t)((width+3)/4) * ((height+3)/4) * bytes > size - 128
            || !allocate_image(image, width, height)) return 0;
    const unsigned char *block = data + 128;
    for (uint32_t y = 0; y < height; y += 4) {
        for (uint32_t x = 0; x < width; x += 4, block += bytes) {
            const unsigned char *colors = block + (dxt == 1 ? 0 : 8);
            uint32_t c0 = av_u16(colors), c1 = av_u16(colors + 2);
            uint32_t palette[4] = {color565(c0), color565(c1), 0, 0};
            int transparent = dxt == 1 && c0 <= c1;
            palette[2] = transparent ? color_mix(palette[0], palette[1], 1, 1) : color_mix(palette[0], palette[1], 2, 1);
            palette[3] = transparent ? 0 : color_mix(palette[0], palette[1], 1, 2);
            unsigned alpha[16];
            block_alpha(block, dxt, alpha);
            uint32_t indices = av_u32(colors + 4);
            for (unsigned i = 0; i < 16; ++i) {
                uint32_t px = x + i%4, py = y + i/4;
                if (px >= width || py >= height) continue;
                uint32_t index = (indices >> (2*i)) & 3u;
                uint32_t a = transparent && index == 3 ? 0 : alpha[i];
                image->pixels[(size_t)py * width + px] = (palette[index] & 0x00ffffffU) | (a << 24);
            }
        }
    }
    return 1;
}

int av_bmp(const unsigned char *data, size_t size, AvImage *image) {
    if (size < 54 || data[0] != 'B' || data[1] != 'M' || av_u32(data+14) < 40) return 0;
    uint32_t start = av_u32(data+10), width = av_u32(data+18);
    int32_t stored_height = (int32_t)av_u32(data+22);
    if (!width || width > 8192 || stored_height < -8192 || stored_height > 8192 || !stored_height) return 0;
    uint32_t height = (uint32_t)(stored_height < 0 ? -stored_height : stored_height);
    uint32_t bits = av_u16(data+28);
    if ((bits != 24 && bits != 32) || av_u32(data+30) != 0 || av_u16(data+26) != 1) return 0;
    size_t pitch = ((size_t)width * (bits/8) + 3) & ~(size_t)3;
    if (start < 54 || start > size || pitch * height > size - start || !allocate_image(image, width, height)) return 0;
    for (uint32_t y = 0; y < height; ++y) {
        const unsigned char *row = data + start + pitch * (stored_height < 0 ? y : height-1-y);
        for (uint32_t x = 0; x < width; ++x, row += bits/8) {
            image->pixels[(size_t)y*width+x] = argb(row[2], row[1], row[0], 255);
        }
    }
    return 1;
}

static void write_u32(unsigned char *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = (unsigned char)(value >> (8*i));
}

int av_save_bmp(const char *path, const AvSurface *surface) {
    if (!surface->pixels) return 0;
    FILE *file = av_fopen(path, "wb");
    if (!file) return 0;
    unsigned char header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    size_t bytes = (size_t)surface->width * (size_t)surface->height * 4;
    write_u32(header+2, (uint32_t)bytes + 54);
    write_u32(header+10, 54);
    write_u32(header+14, 40);
    write_u32(header+18, (uint32_t)surface->width);
    write_u32(header+22, (uint32_t)-surface->height);
    header[26] = 1; header[28] = 32;
    write_u32(header+34, (uint32_t)bytes);
    int ok = fwrite(header, 1, sizeof(header), file) == sizeof(header)
        && fwrite(surface->pixels, 1, bytes, file) == bytes;
    return fclose(file) == 0 && ok;
}
