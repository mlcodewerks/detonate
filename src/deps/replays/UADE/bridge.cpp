// Each helper owns one UADE core. Its frontend and core communicate in memory.
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include "assets.h"
extern "C" {
struct uade_ipc;
void uade_set_peer(uade_ipc *, int, int, int);
int uadecore_main(int, char **);
}
namespace {
struct channel {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<uint8_t> bytes;
    bool closed = false;
};
channel channels[2];
std::thread core;
}
extern "C" ssize_t uade_atomic_read(int fd, const void *dst, size_t count)
{
    auto &c = channels[fd & 1];
    std::unique_lock lock(c.mutex);
    auto *out = (uint8_t *)dst;
    size_t done = 0;
    while (done < count) {
        c.ready.wait(lock, [&] { return c.closed || !c.bytes.empty(); });
        if (c.closed) return done;
        while (done < count && !c.bytes.empty()) { out[done++] = c.bytes.front(); c.bytes.pop_front(); }
    }
    return done;
}
extern "C" ssize_t uade_atomic_write(int fd, const void *src, size_t count)
{
    auto &c = channels[fd & 1];
    std::lock_guard lock(c.mutex);
    if (c.closed) return 0;
    const auto *p = (const uint8_t *)src;
    c.bytes.insert(c.bytes.end(), p, p + count);
    c.ready.notify_one();
    return count;
}
extern "C" int uade_atomic_close(int) { return 0; }
extern "C" int uade_arch_spawn(uade_ipc *ipc, void **, const char *)
{
    core = std::thread([] {
        char a[] = "uadecore", b[] = "-i", c[] = "30577", d[] = "-o", e[] = "30576";
        char *argv[] = {a,b,c,d,e};
        uadecore_main(5, argv);
    });
    core.detach(); // The process lifetime owns the emulator; no shared core state escapes.
    uade_set_peer(ipc, 1, 0x7770, 0x7771);
    return 0;
}
extern "C" void uade_arch_kill_and_wait_uadecore(uade_ipc *, void **)
{
    for (auto &c : channels) { std::lock_guard lock(c.mutex); c.closed = true; c.ready.notify_all(); }
}
extern "C" int uade_filesize(size_t *size, const char *path)
{
    std::error_code ec;
    *size = std::filesystem::file_size(std::filesystem::u8path(path), ec);
    return ec ? -1 : 0;
}
extern "C" char *uade_dirname(char *dst, char *src, size_t size)
{
    auto p = std::filesystem::u8path(src).parent_path().u8string();
    if (!size || p.size() >= size) return nullptr;
    std::memcpy(dst, p.data(), p.size()); dst[p.size()] = 0; return dst;
}
extern "C" int uade_find_amiga_file(char *, size_t, const char *, const char *) { return -1; }
extern "C" FILE *uade_fopen(const char *name, const char *mode)
{
    const char *original = name;
    while (*name == '/') ++name;
    for (const auto &a : assets) if (!std::strcmp(name, a.name)) {
        auto *f = std::tmpfile();
        if (!f) return nullptr;
        if (std::fwrite(a.bytes, 1, a.size, f) != a.size) { std::fclose(f); return nullptr; }
        std::rewind(f); return f;
    }
#ifdef _WIN32
    return _wfopen(std::filesystem::u8path(original).c_str(), std::filesystem::path(mode).c_str());
#else
    return std::fopen(original, mode);
#endif
}
extern "C" int uade_fseek(FILE *f, int p, int whence) { return std::fseek(f,p,whence); }
extern "C" int uade_fread(char *p, int s, int n, FILE *f) { return int(std::fread(p,s,n,f)); }
extern "C" int uade_fwrite(const char *p, int s, int n, FILE *f) { return int(std::fwrite(p,s,n,f)); }
extern "C" int uade_ftell(FILE *f) { return int(std::ftell(f)); }
extern "C" int uade_fclose(FILE *f) { return f ? std::fclose(f) : 0; }
extern "C" char *uade_fgets(char *p, int n, FILE *f) { return std::fgets(p,n,f); }
extern "C" int uade_feof(FILE *f) { return std::feof(f); }
extern "C" FILE *stdioemu_fopen(const char *p, const char *m) { return uade_fopen(p,m); }
extern "C" int stdioemu_fseek(FILE *f, int p, int w) { return uade_fseek(f,p,w); }
extern "C" int stdioemu_fread(char *p, int s, int n, FILE *f) { return uade_fread(p,s,n,f); }
extern "C" int stdioemu_fwrite(const char *p, int s, int n, FILE *f) { return uade_fwrite(p,s,n,f); }
extern "C" int stdioemu_ftell(FILE *f) { return uade_ftell(f); }
extern "C" int stdioemu_fclose(FILE *f) { return uade_fclose(f); }
extern "C" char *stdioemu_fgets(char *p, int n, FILE *f) { return uade_fgets(p,n,f); }
extern "C" int stdioemu_feof(FILE *f) { return uade_feof(f); }

// Detonate owns duration/repeat policy and never writes UADE's user song database.
extern "C" {
struct uade_state;
struct uade_file;
struct uade_content;
uade_content *uade_add_playtime(uade_state *, const char *, uint32_t) { return nullptr; }
void uade_free_song_db(uade_state *) {}
void uade_lookup_song(const uade_file *, uade_state *) {}
int uade_read_content_db(const char *, uade_state *) { return 0; }
int uade_read_song_conf(const char *, uade_state *) { return 0; }
void uade_save_content_db(const char *, uade_state *) {}
int uade_test_silence(void *, size_t, uade_state *) { return 0; }
int uade_update_song_conf(const char *, const char *, const char *) { return -1; }
}
