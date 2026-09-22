#include "backend.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "ken/ken.h"
using std::min;
using std::max;
using INT_PTR = intptr_t;
#define ERR_CODE -1
#pragma GCC diagnostic ignored "-Wnarrowing"
namespace {
uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
bool valid(const std::string &ext, const std::vector<uint8_t> &b, const std::vector<uint8_t> &w) {
    if (ext == "sm" || ext == "snd") {
        const size_t stride = ext == "sm" ? 13 : 7;
        if (b.empty() || b.size() % stride || b.size() / stride > 1500) return false;
        for (size_t p = 0; p < b.size(); p += stride) {
            if (ext == "sm") {
                for (size_t i = 0; i < 12; ++i) if (b[p+i] > 129) return false;
                if (!b[p+12]) return false;
            } else if (b[p+4] < '1' || b[p+4] > '9' || b[p+5] != 13 || b[p+6] != 10) return false;
        }
        return true;
    }
    if (ext == "ksm") {
        if (b.size() < 82) return false;
        const unsigned n = b[80] | unsigned(b[81]) << 8;
        if (!n || n > 8192 || b.size() != 82 + n * 4) return false;
        for (unsigned i = 0; i < 16; ++i) if (b[16+i] > 240 || b[64+i] > 63) return false;
        uint32_t previous = 0;
        for (unsigned i = 0; i < n; ++i) {
            const auto note = le32(&b[82+i*4]);
            if ((note & 63) > 62 || !b[16+((note>>8)&15)] || (note >> 12) < previous) return false;
            previous = note >> 12;
        }
        return previous > (le32(&b[82]) >> 12);
    }
    if (ext != "kdm" || b.size() < 12 || w.size() < 8 || le32(b.data()) || le32(w.data())) return false;
    const auto n = le32(&b[4]), tracks = le32(&b[8]), nw = le32(&w[4]);
    if (!n || n > 8192 || !tracks || tracks > 256 || !nw || nw > 256 || w.size() < 8 + nw*32) return false;
    // The oldest variant omits the final panning-effect array.
    const size_t base = 12 + tracks*4, required = base + n*11;
    if (b.size() != required && b.size() != required-n) return false;
    size_t total = 8 + nw*32;
    for (unsigned i = 0; i < nw; ++i) {
        const auto p = &w[8+i*32];
        const auto len = le32(p+16), start = le32(p+20), loop = le32(p+24);
        const int32_t tuning = int32_t(le32(p+28));
        if (len > 524287 || start > len || loop > len-start || tuning < -747 || tuning > 65535 || len > w.size()-total) return false;
        total += len;
    }
    for (unsigned i = 0; i < tracks; ++i) if (b[12+i] >= nw) return false;
    uint32_t previous = 0;
    for (unsigned i = 0; i < n; ++i) {
        const auto time = le32(&b[base+i*4]);
        if (time < previous || time > 120*60*60 || b[base+n*4+i] >= tracks) return false;
        const auto freq = b[base+n*5+i];
        if (freq > 75 && freq < 242) return false;
        for (unsigned field = 8; field <= 10; ++field)
            if (base+n*field+i < b.size() && b[base+n*field+i] > 16) return false;
        previous = time;
    }
    return previous > le32(&b[base]);
}
struct KenAdlib {
#include "ken/adlibemu.c"
};
#undef PI
struct KenKsm : KenAdlib {
#include "ken/KSMENG.c"
};
struct KenSm : KenAdlib {
#include "ken/smsndeng.c"
};
#undef log2
struct KenKdm {
#include "ken/KDMENG.c"
    ~KenKdm() { freekdmeng(); }
};
#undef PI
struct memory_loader : Loader {
    const std::vector<uint8_t> &data, &waves;
    size_t offset = 0;
    std::string name;
    memory_loader(const std::vector<uint8_t> &d, const std::vector<uint8_t> &w, std::string n)
        : data(d), waves(w), name(std::move(n)) {
        Open = [](Loader *p, const char *) -> Loader * {
            auto &s = *static_cast<memory_loader *>(p);
            return s.waves.empty() ? nullptr : new memory_loader(s.waves, s.waves, "waves.kwv");
        };
        Close = [](Loader *p) { delete static_cast<memory_loader *>(p); };
        Read = [](Loader *p, void *dst, size_t n) {
            auto &s = *static_cast<memory_loader *>(p);
            if (n > s.data.size() - s.offset) return -1;
            std::memcpy(dst, s.data.data() + s.offset, n); s.offset += n; return 0;
        };
        GetName = [](Loader *p) { return static_cast<memory_loader *>(p)->name.c_str(); };
    }
};
template<class T> struct instance final : ken::player {
    T state{};
    unsigned length = 0;
    unsigned duration() const override { return length; }
    void render(int16_t *pcm, unsigned frames) override {
        if constexpr (std::is_same_v<T, KenKdm>) state.kdmrendersound(reinterpret_cast<char *>(pcm), frames * 4);
        else if constexpr (std::is_same_v<T, KenKsm>) state.ksmrendersound(pcm, frames * 4);
        else state.smsndrendersound(pcm, frames * 4);
    }
};
}
std::unique_ptr<ken::player> ken::load(const std::string &ext, const std::vector<uint8_t> &data,
                                     const std::vector<uint8_t> &waves, unsigned rate) {
    if (rate != 44100 || !valid(ext, data, waves)) return {};
    memory_loader loader(data, waves, "song." + ext);
    if (ext == "kdm") {
        auto p = std::make_unique<instance<KenKdm>>(); p->state.kdmsamplerate = rate;
        int duration = p->state.kdmload(&loader); if (duration <= 0) return {};
        p->length = duration; p->state.kdmmusicon(); return p;
    }
    if (ext == "ksm") {
        auto p = std::make_unique<instance<KenKsm>>(); p->state.ksmsamplerate = rate;
        int duration = p->state.ksmload(&loader); if (duration <= 0 || loader.offset != data.size()) return {};
        p->length = duration; p->state.ksmmusicon(); return p;
    }
    auto p = std::make_unique<instance<KenSm>>(); p->state.smsndsamplerate = rate;
    int duration = p->state.smsndload(&loader); if (duration <= 0 || loader.offset != data.size()) return {};
    p->length = duration; p->state.smsndmusicon(); return p;
}
