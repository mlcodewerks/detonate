#include "visualization.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace
{
    template <size_t N>
    void fft(std::array<std::complex<float>, N> &values)
    {
        for (size_t i = 1, j = 0; i < N; ++i)
        {
            size_t bit = N >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap(values[i], values[j]);
        }
        for (size_t length = 2; length <= N; length <<= 1)
        {
            const auto step = std::polar(1.0f, -2.0f * std::numbers::pi_v<float> / float(length));
            for (size_t start = 0; start < N; start += length)
            {
                std::complex<float> phase(1.0f, 0.0f);
                for (size_t j = 0; j < length / 2; ++j)
                {
                    const auto even = values[start + j];
                    const auto odd = phase * values[start + j + length / 2];
                    values[start + j] = even + odd;
                    values[start + j + length / 2] = even - odd;
                    phase *= step;
                }
            }
        }
    }
}

void audio_visualizer::reset()
{
    history_ = {};
    cursor_ = 0;
    data_ = {};
    dirty_ = false;
}

void audio_visualizer::push(const int16_t *stereo, size_t frames)
{
    for (size_t i = 0; i < frames; ++i)
    {
        history_[0][cursor_] = stereo[2 * i] / 32768.0f;
        history_[1][cursor_] = stereo[2 * i + 1] / 32768.0f;
        cursor_ = (cursor_ + 1) % fft_size;
    }
    dirty_ |= frames != 0;
}

const visualization_data &audio_visualizer::snapshot()
{
    if (!dirty_)
        return data_;
    dirty_ = false;
    for (size_t i = 0; i < data_.waveform_size; ++i)
    {
        const size_t index = (cursor_ + fft_size - data_.waveform_size + i) % fft_size;
        data_.left[i] = history_[0][index];
        data_.right[i] = history_[1][index];
    }
    std::array<float, fft_size / 2 + 1> power{};
    for (const auto &channel : history_)
    {
        std::array<std::complex<float>, fft_size> bins;
        for (size_t i = 0; i < fft_size; ++i)
        {
            const float window = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * float(i) / float(fft_size));
            bins[i] = channel[(cursor_ + i) % fft_size] * window;
        }
        fft(bins);
        for (size_t i = 1; i < power.size(); ++i)
            power[i] += std::norm(bins[i]) * 0.5f;
    }
    const float last_bin = 20000.0f * fft_size / 44100.0f;
    for (size_t band = 0; band < data_.band_count; ++band)
    {
        const size_t first = size_t(std::ceil(std::pow(last_bin, float(band) / data_.band_count)));
        const size_t end = size_t(std::ceil(std::pow(last_bin, float(band + 1) / data_.band_count)));
        float peak = 0;
        for (size_t bin = first; bin < std::max(first + 1, end) && bin < power.size(); ++bin)
            peak = std::max(peak, power[bin]);
        const float amplitude = std::sqrt(peak) * (4.0f / fft_size); // Hann coherent gain
        data_.spectrum[band] = std::clamp((20.0f * std::log10(std::max(amplitude, 0.001f)) + 60.0f) / 60.0f, 0.0f, 1.0f);
    }
    return data_;
}
