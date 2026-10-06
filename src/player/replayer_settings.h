#pragma once
#include <array>
#include <filesystem>
#include <span>
#include <string>

namespace replayer_settings
{
    enum id {
        mpt_interpolation, mpt_ramping, mpt_separation, mpt_gain,
        pre_interpolation, pre_mix, pre_delay,
        uade_resampler, uade_filter, uade_led,
        sid_sampling, sid_model, sid_6581_curve, sid_8580_curve,
        ahx_separation, fc_separation, fc_filter, klys_quality,
        gme_eq, gme_treble, gme_bass, adlib_surround, count
    };
    struct option
    {
        id setting;
        const char *key, *group, *label;
        int minimum, maximum, initial;
        const char *choices = nullptr;
        const char *help = nullptr;
        const char *format = "%d";
    };
    using values = std::array<int, count>;
    std::span<const option> options();
    values snapshot();
    void set(id setting, int value);
    void defaults(const char *group = nullptr);
    // Missing files use native defaults. Malformed and unknown entries are ignored.
    bool load(const std::filesystem::path &path, std::string &error);
    bool save(std::string &error);
    std::filesystem::path file_path();
}
