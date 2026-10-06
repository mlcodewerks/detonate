#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace game_fixtures {
using bytes = std::vector<uint8_t>;
inline void put(bytes &data, size_t offset, uint32_t value, size_t count = 4) {
    for (size_t i = 0; i < count; ++i) data[offset + i] = uint8_t(value >> (i * 8));
}
inline void text(bytes &data, size_t offset, const char *value) {
    std::memcpy(data.data() + offset, value, std::strlen(value));
}
inline void save(const std::filesystem::path &path, const bytes &data) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}
inline bytes vgm(bool loop = true) {
    bytes data(0x100);
    text(data, 0, "Vgm "); put(data, 8, 0x171); put(data, 0x0c, 3579545);
    put(data, 0x18, 44100); put(data, 0x34, 0x100 - 0x34);
    // SN76489 channel 0, 440 Hz square wave, full volume, one-second wait.
    data.insert(data.end(), {0x50, 0x8e, 0x50, 0x0f, 0x50, 0x90, 0x61, 0x44, 0xac, 0x66});
    if (loop) { put(data, 0x1c, 0x106 - 0x1c); put(data, 0x20, 44100); }
    const auto gd3 = data.size();
    put(data, 0x14, uint32_t(gd3 - 0x14));
    data.resize(data.size() + 12);
    text(data, gd3, "Gd3 "); put(data, gd3 + 4, 0x100);
    for (char c : std::string("Generated VGM")) { data.push_back(uint8_t(c)); data.push_back(0); }
    data.insert(data.end(), 22, 0); // title terminator and ten remaining GD3 fields
    put(data, gd3 + 8, uint32_t(data.size() - gd3 - 12));
    put(data, 4, uint32_t(data.size() - 4));
    return data;
}
inline bytes gzip(const bytes &data) {
    z_stream stream{};
    if (deflateInit2(&stream, 9, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    bytes result(compressBound(uLong(data.size())) + 32);
    stream.next_in = const_cast<Bytef *>(data.data()); stream.avail_in = uInt(data.size());
    stream.next_out = result.data(); stream.avail_out = uInt(result.size());
    const int status = deflate(&stream, Z_FINISH);
    result.resize(stream.total_out);
    deflateEnd(&stream);
    return status == Z_STREAM_END ? result : bytes{};
}
inline bytes spc(const char *title = "Generated SPC", uint16_t pitch = 0x1000) {
    bytes data(0x10200);
    text(data, 0, "SNES-SPC700 Sound File Data v0.30\x1a\x1a");
    data[0x23] = 0x1a; data[0x24] = 30; put(data, 0x25, 0x200, 2); data[0x2b] = 0xef;
    text(data, 0x2e, title); text(data, 0xa9, "002"); text(data, 0xac, "0000");
    // SPC700: set DSP KON for voice 0, then idle forever.
    const uint8_t code[] = {0x8f, 0x4c, 0xf2, 0x8f, 0x01, 0xf3, 0x2f, 0xfe};
    std::memcpy(data.data() + 0x300, code, sizeof(code));
    data[0x1f1] = 0; // IPL ROM disabled
    put(data, 0x1100, 0x1100, 2); put(data, 0x1102, 0x1100, 2); // BRR start and loop
    const uint8_t brr[] = {0xc3, 0x77, 0x77, 0x33, 0x00, 0x99, 0x99, 0xdd, 0x00};
    std::memcpy(data.data() + 0x1200, brr, sizeof(brr));
    data[0x10100] = data[0x10101] = 0x7f;
    put(data, 0x10102, pitch, 2); data[0x10107] = 0x7f;
    data[0x1010c] = data[0x1011c] = 0x7f; // master volumes
    data[0x1015d] = 0x10; data[0x1016c] = 0x20; // directory and echo disabled
    return data;
}
inline bytes nsf() {
    bytes data(0x82);
    text(data, 0, "NESM\x1a"); data[5] = 1; data[6] = 2; data[7] = 1;
    put(data, 8, 0x8000, 2); put(data, 10, 0x8000, 2); put(data, 12, 0x8001, 2);
    put(data, 0x6e, 16639, 2); data[0x80] = data[0x81] = 0x60; // init/play RTS
    return data;
}
inline bytes nsfe() {
    bytes data{'N', 'S', 'F', 'E'};
    auto chunk = [&](const char *name, const bytes &payload) {
        const auto start = data.size(); data.resize(start + 8);
        put(data, start, uint32_t(payload.size())); text(data, start + 4, name);
        data.insert(data.end(), payload.begin(), payload.end());
    };
    chunk("INFO", {0, 0x80, 0, 0x80, 1, 0x80, 0, 0, 2, 0});
    chunk("auth", {'G','a','m','e',0,'A','r','t','i','s','t',0,0,0});
    chunk("tlbl", {'F','i','r','s','t',0,'S','e','c','o','n','d',0});
    bytes times(8); put(times, 0, 1000); put(times, 4, 2000);
    chunk("time", times);
    chunk("DATA", {0x60, 0x60});
    chunk("NEND", {});
    return data;
}
// A standards-format RAR4 archive with stored entries; no external archiver needed.
inline bytes rsn(const std::vector<std::pair<std::string, bytes>> &entries, bool solid = false) {
    bytes result{'R', 'a', 'r', '!', 0x1a, 7, 0};
    auto header = [&](bytes data) {
        put(data, 0, uint32_t(crc32(0, data.data() + 2, uInt(data.size() - 2))), 2);
        result.insert(result.end(), data.begin(), data.end());
    };
    bytes main(13); main[2] = 0x73; put(main, 3, solid ? 8 : 0, 2); put(main, 5, 13, 2); header(main);
    for (const auto &[name, payload] : entries) {
        bytes file(32 + name.size()); file[2] = 0x74; put(file, 3, 0x8000, 2);
        put(file, 5, uint32_t(file.size()), 2); put(file, 7, uint32_t(payload.size()));
        put(file, 11, uint32_t(payload.size())); file[15] = 3;
        put(file, 16, uint32_t(crc32(0, payload.data(), uInt(payload.size()))));
        file[24] = 20; file[25] = 0x30; put(file, 26, uint32_t(name.size()), 2);
        put(file, 28, 0100644); text(file, 32, name.c_str()); header(file);
        result.insert(result.end(), payload.begin(), payload.end());
    }
    bytes end(7); end[2] = 0x7b; put(end, 5, 7, 2); header(end);
    return result;
}
}
