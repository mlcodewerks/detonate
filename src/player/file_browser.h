#pragma once
#include "archive_reader.h"
#include "playlist.h"
#include "audiodecode.h"
#include <filesystem>
#include <memory>

struct browser_entry
{
    std::string name, key;
    bool directory = false, archive = false;
    size_t member = 0;
    std::vector<subsong_tags> subsongs{};
    subsong_tags tags{};
    int track = -1;
};

class file_browser
{
    struct archive_level
    {
        std::string name, prefix;
        std::shared_ptr<const std::vector<archive_member>> members;
    };
    std::filesystem::path directory_;
    std::vector<archive_level> archives_;
    std::vector<browser_entry> entries_;
    std::string error_;
    std::string subsong_name_;
    playlist_item subsong_source_;
    std::vector<browser_entry> subsong_entries_;
    void list();
    void enter_archive(const std::vector<uint8_t> &bytes, const std::string &name);

public:
    bool open(const std::filesystem::path &path);
    bool activate(size_t index, bool background_playback = false);
    void up();
    void refresh();
    std::string location() const;
    const std::filesystem::path &directory() const { return directory_; }
    const std::vector<browser_entry> &entries() const { return entries_; }
    const std::string &error() const { return error_; }
    bool in_archive() const { return !archives_.empty(); }
    bool in_subsongs() const { return !subsong_entries_.empty(); }
    std::vector<playlist_item> playlist() const;
};
