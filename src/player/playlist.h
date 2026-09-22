#pragma once
#include "archive_reader.h"
#include <algorithm>
#include <memory>
#include <numeric>
#include <random>

struct playlist_item
{
    std::string key;
    std::shared_ptr<const std::vector<archive_member>> members;
    size_t member = 0;
    std::string source{};
    int track = -1;
};

class directory_playlist
{
    std::vector<playlist_item> items_;
    std::vector<size_t> order_;
    size_t position_ = 0;
    bool active_ = false, started_ = false, shuffle_ = false;
    std::mt19937 random_{std::random_device{}()};

public:
    enum class mode
    {
        song,
        repeat_song,
        directory,
        repeat_directory
    };
    mode playback = mode::directory;
    void stop()
    {
        active_ = started_ = false;
        items_.clear();
        order_.clear();
        position_ = 0;
    }
    bool shuffled() const { return shuffle_; }
    void shuffle(bool enabled)
    {
        shuffle_ = enabled;
        if (position_ + 1 >= order_.size())
            return;
        auto first = order_.begin() + position_ + 1;
        if (enabled)
            std::shuffle(first, order_.end(), random_);
        else
            std::sort(first, order_.end());
    }
    const playlist_item *current() const
    {
        return active_ && position_ < order_.size() ? &items_[order_[position_]] : nullptr;
    }
    const playlist_item *start(std::vector<playlist_item> items, const std::string &key)
    {
        stop();
        items_ = std::move(items);
        auto it = std::find_if(items_.begin(), items_.end(), [&](const auto &item)
                               { return item.key == key; });
        if (it == items_.end())
        {
            stop();
            return nullptr;
        }
        const size_t selected = size_t(it - items_.begin());
        order_.resize(items_.size());
        std::iota(order_.begin(), order_.end(), 0);
        if (shuffle_)
        {
            std::swap(order_[0], order_[selected]);
            std::shuffle(order_.begin() + 1, order_.end(), random_);
        }
        else
            position_ = selected;
        active_ = true;
        return current();
    }

    const playlist_item *poll(bool loading, bool playing, bool paused, bool error)
    {
        if (!active_ || loading || paused)
            return nullptr;
        if (error)
        {
            stop();
            return nullptr;
        }
        if (playing)
        {
            started_ = true;
            return nullptr;
        }
        if (!started_)
            return nullptr;
        started_ = false;
        if (playback == mode::song || playback == mode::repeat_song)
            return nullptr;
        if (++position_ == order_.size())
        {
            if (playback != mode::repeat_directory)
            {
                stop();
                return nullptr;
            }
            const auto last = order_.back();
            position_ = 0;
            std::iota(order_.begin(), order_.end(), 0);
            if (shuffle_)
            {
                std::shuffle(order_.begin(), order_.end(), random_);
                if (order_.size() > 1 && order_.front() == last)
                    std::swap(order_[0], order_[1]);
            }
        }
        return current();
    }
};
