#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

struct visualization_data {
    static constexpr size_t waveform_size = 512, band_count = 32, fft_bins = 1024;
    std::array<float, waveform_size> left{}, right{};
    // Logarithmic bands from one FFT bin (44100 / 2048 Hz) to 20 kHz,
    // normalized from -60 dBFS to 0 dBFS.
    std::array<float, band_count> spectrum{};
    // Linear bins from a 2048-point FFT, covering 0 to the Nyquist frequency.
    // Same dB normalization as spectrum; stereo power avoids phase cancellation.
    std::array<float, fft_bins> fft{};
};

// Fed only with PCM accepted by the frontend, on the retro_run thread.
// Analysis is lazy, so hiding the visualization avoids FFT work.
class audio_visualizer {
    static constexpr size_t fft_size = 2048;
    std::array<std::array<float, fft_size>, 2> history_{};
    size_t cursor_ = 0;
    bool dirty_ = false;
    visualization_data data_{};
public:
    void reset();
    void push(const int16_t *stereo, size_t frames);
    const visualization_data &snapshot();
};

const visualization_data &music_visualization();
