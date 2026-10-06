#include "replayer_settings.h"
#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <mutex>

namespace replayer_settings
{
    namespace
    {
        constexpr option catalog[] = {
            {mpt_interpolation, "openmpt.interpolation", "OpenMPT", "Interpolation", 0, 4, 0, "Recommended\0None\0Linear\0Cubic\0Sinc\0"},
            {mpt_ramping, "openmpt.ramping", "OpenMPT", "Volume ramping", -1, 10, -1, nullptr, "-1: recommended; 0: off; 1-10: increasingly smooth ramps."},
            {mpt_separation, "openmpt.separation", "OpenMPT", "Stereo separation", 0, 200, 100, nullptr, "100% preserves the original stereo separation.", "%d%%"},
            {mpt_gain, "openmpt.gain", "OpenMPT", "Gain", -12, 12, 0, nullptr, nullptr, "%d dB"},
            {pre_interpolation, "pretracker.interpolation", "PreTracker", "Interpolation", 0, 1, 0, "BLEP\0Sinc\0"},
            {pre_mix, "pretracker.mix", "PreTracker", "Stereo crossfeed", 0, 100, 0, nullptr, "0%: full stereo; 100%: mono.", "%d%%"},
            {pre_delay, "pretracker.delay", "PreTracker", "Stereo delay", 0, 1400, 0, nullptr, "Haas delay between the stereo channels.", "%d us"},
            {uade_resampler, "uade.resampler", "UADE", "Resampling", 0, 2, 0, "Default (anti-alias)\0Sinc (BLEP)\0None\0"},
            {uade_filter, "uade.filter", "UADE", "Amiga filter", 0, 2, 0, "A500\0A1200\0Off\0"},
            {uade_led, "uade.led", "UADE", "LED filter", 0, 2, 0, "Follow song\0Force off\0Force on\0"},
            {sid_sampling, "sid.sampling", "SID", "Sampling", 0, 1, 0, "Resample + interpolate\0Interpolate\0"},
            {sid_model, "sid.model", "SID", "Chip model", 0, 2, 0, "Follow song\0MOS 6581\0MOS 8580\0"},
            {sid_6581_curve, "sid.curve6581", "SID", "6581 filter curve", 0, 100, 50, nullptr, "50% is the default filter curve.", "%d%%"},
            {sid_8580_curve, "sid.curve8580", "SID", "8580 filter curve", 0, 100, 50, nullptr, "50% is the default filter curve.", "%d%%"},
            {ahx_separation, "hively.separation", "Hively / AHX", "AHX stereo separation", 0, 100, 50, nullptr, "Applies to AHX songs. HVL songs retain their own panning.", "%d%%"},
            {fc_separation, "futurecomposer.separation", "Future Composer", "Stereo separation", 0, 100, 100, nullptr, nullptr, "%d%%"},
            {fc_filter, "futurecomposer.filter", "Future Composer", "Low-pass filter", 0, 1, 0},
            {klys_quality, "klystrack.quality", "klystrack", "Oversampling", 0, 4, 4, "1x\0" "2x\0" "4x\0" "8x\0" "16x\0"},
            {gme_eq, "gme.eq", "Game Music Emu", "Override equalizer", 0, 1, 0, nullptr, "Supported chip emulators use these treble and bass settings when enabled."},
            {gme_treble, "gme.treble", "Game Music Emu", "Treble", -50, 5, 0, nullptr, nullptr, "%d dB"},
            {gme_bass, "gme.bass", "Game Music Emu", "Bass cutoff", 20, 500, 80, nullptr, nullptr, "%d Hz"},
            {adlib_surround, "adlib.surround", "AdLib", "Harmonic stereo", 0, 1, 0, nullptr, "Adds stereo width with a slightly detuned second OPL chip."},
        };
        static_assert(std::size(catalog) == count);
        values native_defaults()
        {
            values v{};
            for (const auto &o : catalog) v[o.setting] = o.initial;
            return v;
        }
        std::mutex mutex;
        values current = native_defaults();
        std::filesystem::path path = "detonate-replayers.ini";
    }
    std::span<const option> options() { return catalog; }
    values snapshot() { std::lock_guard lock(mutex); return current; }
    void set(id setting, int value)
    {
        if (setting < 0 || setting >= count) return;
        const auto &o = catalog[setting];
        std::lock_guard lock(mutex);
        current[setting] = std::clamp(value, o.minimum, o.maximum);
    }
    void defaults(const char *group)
    {
        std::lock_guard lock(mutex);
        for (const auto &o : catalog)
            if (!group || !std::strcmp(group, o.group)) current[o.setting] = o.initial;
    }
    std::filesystem::path file_path() { std::lock_guard lock(mutex); return path; }
    bool load(const std::filesystem::path &filename, std::string &error)
    {
        error.clear();
        values next = native_defaults();
        {
            std::lock_guard lock(mutex);
            // Keep the selected destination even when reading fails, so Save
            // never silently falls back to a different directory.
            path = filename;
            current = next;
        }
        std::error_code ec;
        const bool exists = std::filesystem::exists(filename, ec);
        if (ec) { error = "Cannot read replayer settings: " + ec.message(); return false; }
        if (exists)
        {
            std::ifstream in(filename);
            if (!in) { error = "Cannot open replayer settings."; return false; }
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto sep = line.find('=');
                if (sep == std::string::npos) continue;
                int value;
                auto begin = line.data() + sep + 1, end = line.data() + line.size();
                auto parsed = std::from_chars(begin, end, value);
                if (parsed.ec != std::errc{} || parsed.ptr != end) continue;
                for (const auto &o : catalog)
                    if (line.compare(0, sep, o.key) == 0 && std::strlen(o.key) == sep)
                        next[o.setting] = std::clamp(value, o.minimum, o.maximum);
            }
            if (in.bad()) { error = "Cannot read replayer settings."; return false; }
        }
        std::lock_guard lock(mutex);
        path = filename;
        current = next;
        return true;
    }
    bool save(std::string &error)
    {
        error.clear();
        const auto v = snapshot();
        const auto destination = file_path();
        auto temporary = destination; temporary += ".tmp";
        std::ofstream out(temporary, std::ios::trunc);
        out << "# Detonate replayer defaults\n";
        for (const auto &o : catalog) out << o.key << '=' << v[o.setting] << '\n';
        out.close();
        if (!out) { error = "Cannot write replayer settings."; return false; }
        std::error_code ec;
        // Windows rename cannot replace an existing file. Keep the previous file
        // until the complete replacement has been written successfully.
        std::filesystem::copy_file(temporary, destination, std::filesystem::copy_options::overwrite_existing, ec);
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        if (ec) { error = "Cannot save replayer settings: " + ec.message(); return false; }
        return true;
    }
}
