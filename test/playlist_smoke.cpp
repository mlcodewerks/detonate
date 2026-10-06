#include "playlist.h"
#include <cstdio>
#include <cstdlib>
#include <set>

static void check(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::vector<playlist_item> songs()
{
    return {{"a.wav", {}, 0}, {"b.wav", {}, 0}, {"c.wav", {}, 0}, {"d.wav", {}, 0}};
}
static const playlist_item *finish(directory_playlist &p)
{
    check(!p.poll(false, true, false, false), "playing song does not advance");
    return p.poll(false, false, false, false);
}
int main()
{
    using mode = directory_playlist::mode;
    directory_playlist p;
    check(p.start(songs(), "b.wav")->key == "b.wav", "start selected song");
    check(!p.poll(true, false, false, false), "loading does not advance");
    check(!p.poll(false, false, false, false), "must observe started song");
    p.poll(false, true, false, false);
    check(!p.poll(false, false, true, false), "pause does not advance");
    check(!p.poll(true, false, false, false), "background seek does not advance");
    check(finish(p)->key == "c.wav" && finish(p)->key == "d.wav", "directory follows browser order");
    check(!finish(p) && !p.current(), "stop after last directory song");
    check(!p.poll(false, false, false, false), "EOF remains stopped");
    for (mode m : {mode::song, mode::repeat_song}) {
        p.playback = m; p.start(songs(), "a.wav");
        check(!finish(p) && p.current()->key == "a.wav", "song modes never advance directory");
    }
    p.playback = mode::repeat_directory; p.start(songs(), "d.wav");
    check(finish(p)->key == "a.wav", "repeat directory wraps");
    p.stop(); check(!p.poll(false, false, false, false) && !p.current(), "Stop cancels repeat");
    p.start(songs(), "a.wav"); p.poll(false, true, false, false);
    check(!p.poll(false, false, false, true) && !p.current(), "decoder error stops playlist");
    p.shuffle(true); p.start(songs(), "c.wav");
    std::set<std::string> first;
    for (unsigned i = 0; i < 4; ++i) {
        check(first.insert(p.current()->key).second, "shuffle has no duplicates in pass");
        const auto previous = p.current()->key;
        const auto *next = finish(p);
        check(next && next->key != previous, "shuffle avoids immediate repeats across passes");
    }
    check(first.size() == 4, "shuffle visits entire directory");
    p.playback = mode::directory;
    std::set<std::string> second;
    while (p.current()) { second.insert(p.current()->key); finish(p); }
    check(second.size() == 4, "shuffled once stops after a full pass");
    p.start(songs(), "a.wav"); p.shuffle(false);
    check(finish(p)->key == "b.wav", "disable shuffle restores remaining order");
    p.playback = mode::repeat_directory; p.start({{"only.wav", {}, 0}}, "only.wav");
    check(finish(p)->key == "only.wav", "one-song directory repeats");
    check(!p.start({}, "missing") && !p.current(), "empty directory");
    auto members = std::make_shared<std::vector<archive_member>>();
    members->push_back({"dir/a.wav", {1, 2, 3}});
    std::weak_ptr<const std::vector<archive_member>> lifetime = members;
    p.start({{"dir/a.wav", members, 0}}, "dir/a.wav"); members.reset();
    check(!lifetime.expired() && p.current()->members->at(0).bytes.size() == 3, "archive snapshot owns companions after navigation");
    p.stop(); check(lifetime.expired(), "Stop releases archive snapshot");
    std::puts("Playlist order, EOF, pause, seek, Stop, shuffle, repeat and archive ownership passed");
}
