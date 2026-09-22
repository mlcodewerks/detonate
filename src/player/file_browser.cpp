#include "file_browser.h"
#include "audiodecode.h"
#include "formats/game_music_io.h"
#include <map>

namespace fs = std::filesystem;
std::vector<playlist_item> file_browser::playlist() const
{
    std::vector<playlist_item> result;
    for (const auto &entry : entries_)
        if (!entry.directory && !entry.archive)
        {
            auto item = in_subsongs() ? subsong_source_ : playlist_item{entry.key, archives_.empty() ? nullptr : archives_.back().members, entry.member};
            item.key = entry.key;
            item.track = entry.track;
            result.push_back(std::move(item));
        }
    return result;
}
namespace
{
    std::string utf8(const fs::path &path)
    {
        const auto text = path.u8string();
        return {text.begin(), text.end()};
    }
    std::string normalized(std::string name)
    {
        std::replace(name.begin(), name.end(), '\\', '/');
        if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos)
            return {};
        std::string result;
        size_t start = 0;
        while (start < name.size())
        {
            const auto end = name.find('/', start);
            const auto part = name.substr(start, end == std::string::npos ? end : end - start);
            if (part == "..")
                return {};
            if (!part.empty() && part != ".")
            {
                if (!result.empty())
                    result += '/';
                result += part;
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        return result;
    }
}

void file_browser::list()
{
    if (in_subsongs())
    {
        entries_ = subsong_entries_;
        return;
    }
    std::vector<browser_entry> next;
    if (archives_.empty())
    {
        for (const auto &entry : fs::directory_iterator(directory_))
        {
            std::error_code ec;
            const bool dir = entry.is_directory(ec);
            if (ec)
                continue;
            const auto key = utf8(entry.path());
            const bool container = !dir && is_music_archive(key);
            if (!dir && (!entry.is_regular_file(ec) || ec || (!container && !auddecode_supports(key.c_str()))))
                continue;
            next.push_back({utf8(entry.path().filename()), key, dir, container, 0});
        }
    }
    else
    {
        const auto &level = archives_.back();
        std::map<std::string, browser_entry> children;
        for (size_t i = 0; i < level.members->size(); ++i)
        {
            const auto &name = (*level.members)[i].name;
            if (name.empty() || !name.starts_with(level.prefix))
                continue;
            if (!is_music_archive(name) && !auddecode_supports(name.c_str()))
                continue;
            const auto relative = name.substr(level.prefix.size());
            const auto slash = relative.find('/');
            const auto child = relative.substr(0, slash);
            if (child.empty())
                continue;
            const bool dir = slash != std::string::npos;
            auto record = browser_entry{child, level.prefix + child, dir, !dir && is_music_archive(child), i};
            auto it = children.find(child);
            if (it == children.end() || dir)
                children[child] = std::move(record);
        }
        for (auto &[key, child] : children)
            next.push_back(std::move(child));
    }
    for (auto &entry : next)
        if (!entry.directory && !entry.archive)
        {
            // Invalid audio remains visible so playback can report its error.
            try
            {
                entry.subsongs = music_subsongs(entry.key,
                                                archives_.empty() ? nullptr : archives_.back().members, entry.member);
            }
            catch (const std::exception &)
            {
                entry.subsongs.clear();
            }
            entry.archive = !entry.subsongs.empty();
        }
    std::sort(next.begin(), next.end(), [](const auto &a, const auto &b)
              {
        const bool a_dir = a.directory || a.archive, b_dir = b.directory || b.archive;
        return a_dir != b_dir ? a_dir : a.name < b.name; });
    entries_ = std::move(next);
}

void file_browser::enter_archive(const std::vector<uint8_t> &bytes, const std::string &name)
{
    if (archives_.size() >= 8)
        throw std::runtime_error("Archive nesting exceeds eight levels.");
    auto members = read_music_archive(bytes, name);
    size_t total = 0;
    for (const auto &level : archives_)
        for (const auto &member : *level.members)
            total += member.bytes.size();
    for (auto &member : members)
    {
        total += member.bytes.size();
        member.name = normalized(member.name);
    }
    if (total > 256 * 1024 * 1024)
        throw std::runtime_error("Open archives exceed the 256 MiB size limit.");
    archives_.push_back({name, {}, std::make_shared<const std::vector<archive_member>>(std::move(members))});
    try
    {
        list();
    }
    catch (...)
    {
        archives_.pop_back();
        throw;
    }
}

bool file_browser::open(const fs::path &path)
{
    auto previous_browser = *this;
    try
    {
        subsong_entries_.clear();
        subsong_source_ = {};
        subsong_name_.clear();
        const auto absolute = fs::absolute(path).lexically_normal();
        if (fs::is_directory(absolute))
        {
            auto previous_directory = directory_;
            auto previous = std::move(archives_);
            archives_.clear();
            directory_ = absolute;
            try
            {
                list();
            }
            catch (...)
            {
                directory_ = std::move(previous_directory);
                archives_ = std::move(previous);
                throw;
            }
        }
        else if (is_music_archive(utf8(absolute)))
        {
            // Read before discarding the current location so a failed open is recoverable.
            auto bytes = read_game_music(utf8(absolute).c_str());
            auto previous = std::move(archives_);
            archives_.clear();
            try
            {
                enter_archive(bytes, utf8(absolute.filename()));
            }
            catch (...)
            {
                archives_ = std::move(previous);
                throw;
            }
            directory_ = absolute.parent_path();
        }
        else
        {
            if (!open(absolute.parent_path()))
                throw std::runtime_error(error_);
            const auto key = utf8(absolute);
            for (size_t i = 0; i < entries_.size(); ++i)
                if (entries_[i].key == key && !entries_[i].subsongs.empty())
                    return activate(i);
        }
        error_.clear();
        return true;
    }
    catch (const std::exception &e)
    {
        *this = std::move(previous_browser);
        error_ = e.what();
        return false;
    }
}

bool file_browser::activate(size_t index, bool background_playback)
{
    if (index >= entries_.size())
        return false;
    const auto entry = entries_[index];
    try
    {
        error_.clear();
        if (!entry.subsongs.empty())
        {
            std::vector<browser_entry> tracks;
            for (size_t i = 0; i < entry.subsongs.size(); ++i)
            {
                browser_entry song;
                song.name = std::to_string(i + 1) + ". " + entry.subsongs[i].title;
                song.key = entry.key + "#subsong=" + std::to_string(i);
                song.track = int(i);
                song.tags = entry.subsongs[i];
                tracks.push_back(std::move(song));
            }
            subsong_source_ = {entry.key, archives_.empty() ? nullptr : archives_.back().members,
                               entry.member, entry.key};
            subsong_name_ = entry.name;
            subsong_entries_ = std::move(tracks);
            list();
            return true;
        }
        if (in_subsongs())
        {
            const auto &source = subsong_source_;
            if (background_playback)
            {
                if (source.members)
                    music_play_archive_async(source.members, source.member, entry.track);
                else
                    music_play_async(source.source, entry.track);
                return true;
            }
            bool loaded;
            if (source.members)
            {
                const auto &member = source.members->at(source.member);
                loaded = music_play_memory(member.name, member.bytes, nullptr, source.members.get());
            }
            else
                loaded = music_play(source.source.c_str());
            return loaded && music_settrack(unsigned(entry.track));
        }
        if (archives_.empty())
        {
            if (entry.directory || entry.archive)
                return open(fs::path(reinterpret_cast<const char8_t *>(entry.key.c_str())));
            if (background_playback)
            {
                music_play_async(entry.key);
                return true;
            }
            return music_play(entry.key.c_str());
        }
        if (entry.directory)
        {
            auto previous = archives_.back().prefix;
            archives_.back().prefix = entry.key + '/';
            try
            {
                list();
            }
            catch (...)
            {
                archives_.back().prefix = std::move(previous);
                throw;
            }
        }
        else if (entry.archive)
        {
            const auto &member = (*archives_.back().members)[entry.member];
            enter_archive(member.bytes, entry.name);
        }
        else
        {
            const auto &level = archives_.back();
            if (background_playback)
            {
                music_play_archive_async(level.members, entry.member);
                return true;
            }
            const auto &member = (*level.members)[entry.member];
            const std::vector<uint8_t> *rom = nullptr;
            const auto slash = member.name.rfind('/');
            const auto rom_name = (slash == std::string::npos ? "" : member.name.substr(0, slash + 1)) + "yrw801.rom";
            for (const auto &sibling : *level.members)
                if (sibling.name == rom_name)
                    rom = &sibling.bytes;
            return music_play_memory(member.name, member.bytes, rom, level.members.get());
        }
        error_.clear();
        return true;
    }
    catch (const std::exception &e)
    {
        error_ = e.what();
        return false;
    }
}

void file_browser::up()
{
    if (in_subsongs())
    {
        auto previous = std::move(subsong_entries_);
        subsong_entries_.clear();
        try
        {
            list();
            subsong_source_ = {};
            subsong_name_.clear();
            error_.clear();
        }
        catch (const std::exception &e)
        {
            subsong_entries_ = std::move(previous);
            error_ = e.what();
        }
        return;
    }
    if (archives_.empty())
    {
        open(directory_.parent_path());
        return;
    }
    try
    {
        auto &prefix = archives_.back().prefix;
        if (!prefix.empty())
        {
            auto previous = prefix;
            const auto slash = prefix.rfind('/', prefix.size() - 2);
            prefix = slash == std::string::npos ? "" : prefix.substr(0, slash + 1);
            try
            {
                list();
            }
            catch (...)
            {
                prefix = std::move(previous);
                throw;
            }
        }
        else
        {
            auto previous = std::move(archives_.back());
            archives_.pop_back();
            try
            {
                list();
            }
            catch (...)
            {
                archives_.push_back(std::move(previous));
                throw;
            }
        }
        error_.clear();
    }
    catch (const std::exception &e)
    {
        error_ = e.what();
    }
}
void file_browser::refresh()
{
    try
    {
        list();
        error_.clear();
    }
    catch (const std::exception &e)
    {
        error_ = e.what();
    }
}
std::string file_browser::location() const
{
    std::string text = utf8(directory_);
    for (const auto &level : archives_)
        text += " / " + level.name + " / " + level.prefix;
    if (in_subsongs())
        text += " / " + subsong_name_;
    return text;
}
