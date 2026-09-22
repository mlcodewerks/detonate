#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// All output is interleaved stereo float PCM. Counts are frames; storage
// belongs to the decoder and remains valid until its next operation.
class auddecode
{
public:
    virtual ~auddecode() = default;
    virtual bool open(const char *filename, float *rate, bool loop) = 0;
    virtual bool open_memory(const std::string &, const std::vector<uint8_t> &, float *, bool) { return false; }
    virtual bool seek(unsigned ms) = 0;
    virtual void stop() = 0;
    virtual bool is_playing() = 0;
    virtual bool is_module() const { return false; }
    virtual bool supports_looping() const { return is_module(); }
    virtual void set_loop(bool) {}
    virtual unsigned song_duration() = 0;
    virtual const char *song_title() { return nullptr; }
    virtual const char *song_artist() { return nullptr; }
    virtual const char *song_album() { return nullptr; }
    virtual unsigned track_count() const { return 1; }
    virtual unsigned current_track() const { return 0; }
    virtual std::string track_title(unsigned) const { return "Track 1"; }
    virtual bool select_track(unsigned index) { return index == 0 && seek(0); }
    virtual std::vector<std::string> file_types() = 0;
    virtual void mix(float *&buffer, unsigned &frames) = 0;
};
bool music_isplaying();
bool music_ispaused();
void music_pause(bool paused);
void music_repeat(bool repeat);
bool music_getrepeat();
bool music_islooping();
unsigned music_trackcount();
unsigned music_currenttrack();
std::string music_tracktitle(unsigned index);
std::string music_title();
bool music_settrack(unsigned index);
void music_stop();
bool music_play(const char *filename);
struct archive_member;
bool music_play_memory(const std::string &name, const std::vector<uint8_t> &bytes,
                       const std::vector<uint8_t> *rom = nullptr,
                       const std::vector<archive_member> *companions = nullptr);
void music_run();
void music_play_async(std::string filename, int track = -1);
void music_play_archive_async(std::shared_ptr<const std::vector<archive_member>> members, size_t index, int track = -1);
void music_stop_async();
void music_settrack_async(unsigned index);
void music_setposition_async(uint64_t ms);
void music_poll();
bool music_loading();
void music_wait();
uint32_t music_getduration();
uint32_t music_getposition();
void music_setposition(uint64_t ms);
const std::string &music_error();
std::string auddecode_formats();
bool auddecode_supports(const char *filename);
auddecode *make_decoder(const char *filename, float *rate);
struct subsong_tags
{
    std::string title, artist, album;
    unsigned duration = 0;
};
std::vector<subsong_tags> music_subsongs(const std::string &name,
                                         std::shared_ptr<const std::vector<archive_member>> members = {}, size_t member = 0);
auddecode *create_common();
auddecode *create_wav();
auddecode *create_mpc();
auddecode *create_wv();
auddecode *create_vgm();
auddecode *create_gme();

auddecode *create_sid();
auddecode *create_v2m();
auddecode *create_organya();
auddecode *create_klystrack();
auddecode *create_tfmx();
auddecode *create_hively();
auddecode *create_psx();
auddecode *create_sega();
auddecode *create_qsf();
auddecode *create_usf();
auddecode *create_gsf();
auddecode *create_snsf();
auddecode *create_2sf();

auddecode *create_futurecomposer();
auddecode *create_ken();
