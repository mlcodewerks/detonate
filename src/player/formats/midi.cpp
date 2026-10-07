#include "decoder_base.h"
#include "../midi_resources.h"
#include "../replayer_settings.h"
#include <spessasynth/spessasynth.h>
#include "88lib/c_interface.h"
#include "mu2000.h"
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <span>
#include <stdexcept>

namespace
{
    std::mutex resources_mutex, roland_mutex;
    std::once_flag spessa_tables;
    std::filesystem::path resources = std::filesystem::path("system") / "detonatemidi";
    template<class T, auto Free> using owned = std::unique_ptr<T, decltype(Free)>;
    using midi_file = owned<SS_MIDIFile, ss_midi_free>;
    using sequencer = owned<SS_Sequencer, ss_sequencer_free>;
    using processor = owned<SS_Processor, ss_processor_free>;
    using soundbank = owned<SS_SoundBank, ss_soundbank_free>;
    using ss_file = owned<SS_File, ss_file_close>;
    using roland = owned<emu88_data, emu88_free_context>;

    std::string utf8(const std::filesystem::path &path)
    {
        const auto s = path.u8string();
        return {s.begin(), s.end()};
    }
    std::vector<uint8_t> resource_bytes(const std::filesystem::path &path, size_t limit)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || !size || size > limit)
            throw std::runtime_error("Missing or invalid MIDI resource: " + utf8(path));
        auto bytes = read_audio_file(utf8(path).c_str());
        if (bytes.size() != size) throw std::runtime_error("Cannot read MIDI resource: " + utf8(path));
        return bytes;
    }

    // Check chunk and event bounds before handing untrusted files to the native parser.
    bool valid_smf(std::span<const uint8_t> b)
    {
        auto be = [&](size_t p, unsigned n) {
            uint32_t v = 0; while (n--) v = (v << 8) | b[p++]; return v;
        };
        if (b.size() < 14 || std::memcmp(b.data(), "MThd", 4)) return false;
        const auto header = be(4, 4), format = be(8, 2), tracks = be(10, 2), division = be(12, 2);
        if (header < 6 || header > b.size() - 8 || format > 2 || !tracks || (format == 0 && tracks != 1) || !division) return false;
        if (division & 0x8000)
        {
            const int fps = -int(int8_t(division >> 8));
            if (!(division & 255) || (fps != 24 && fps != 25 && fps != 29 && fps != 30)) return false;
        }
        size_t p = 8 + header;
        for (unsigned track = 0; track < tracks; ++track)
        {
            if (b.size() - p < 8 || std::memcmp(b.data() + p, "MTrk", 4)) return false;
            const size_t size = be(p + 4, 4); p += 8;
            if (size > b.size() - p) return false;
            const size_t end = p + size;
            uint8_t running = 0; bool ended = false;
            auto vlq = [&](uint32_t &value) {
                value = 0;
                for (unsigned i = 0; i < 4 && p < end; ++i)
                {
                    uint8_t c = b[p++]; value = (value << 7) | (c & 127);
                    if (!(c & 128)) return true;
                }
                return false;
            };
            while (p < end)
            {
                uint32_t delta;
                if (!vlq(delta) || p == end) return false;
                uint8_t status = b[p];
                if (status & 128) ++p;
                else if (!(status = running)) return false;
                if (status < 0xf0)
                {
                    running = status;
                    const unsigned n = (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 1 : 2;
                    if (end - p < n) return false;
                    for (unsigned i = 0; i < n; ++i) if (b[p++] & 128) return false;
                }
                else
                {
                    running = 0;
                    uint8_t meta = 0;
                    if (status == 0xff) { if (p == end) return false; meta = b[p++]; }
                    else if (status != 0xf0 && status != 0xf7) return false;
                    uint32_t length;
                    if (!vlq(length) || length > end - p) return false;
                    if (status == 0xff && meta == 0x2f) { if (length) return false; ended = true; }
                    p += length;
                }
            }
            if (!ended) return false;
        }
        return p == b.size();
    }
    bool valid_midi(std::span<const uint8_t> b)
    {
        if (b.size() >= 4 && !std::memcmp(b.data(), "MThd", 4)) return valid_smf(b);
        if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "RMID", 4)) return false;
        auto le = [&](size_t p) { return uint32_t(b[p]) | uint32_t(b[p+1]) << 8 | uint32_t(b[p+2]) << 16 | uint32_t(b[p+3]) << 24; };
        if (le(4) != b.size() - 8) return false;
        bool found = false;
        for (size_t p = 12; p < b.size();)
        {
            if (b.size() - p < 8) return false;
            const size_t length = le(p + 4);
            if (length + (length & 1) > b.size() - p - 8) return false;
            if (!std::memcmp(b.data() + p, "data", 4))
            {
                if (found || !valid_smf(b.subspan(p + 8, length))) return false;
                found = true;
            }
            p += 8 + length + (length & 1);
        }
        return found;
    }

    void normalize_smpte(SS_MIDIFile &midi)
    {
        if (!(midi.time_division & 0x8000)) return;
        const int fps_code = -int(int8_t(midi.time_division >> 8));
        const double fps = fps_code == 29 ? 30000.0 / 1001.0 : double(fps_code);
        const double scale = 65534.0 / (fps * (midi.time_division & 255));
        // A fixed 120 BPM map with high PPQN preserves SMPTE times within 8 us.
        // Tempo metas have no timing meaning for SMPTE divisions.
        for (size_t t = 0; t < midi.track_count; ++t)
            for (size_t e = 0; e < midi.tracks[t].event_count; ++e)
            {
                auto &event = midi.tracks[t].events[e];
                event.ticks = size_t(std::llround(event.ticks * scale));
                if (event.status_byte == SS_META_SET_TEMPO && event.data_length == 3)
                { event.data[0] = 7; event.data[1] = 0xa1; event.data[2] = 0x20; }
            }
        midi.time_division = 32767;
        ss_midi_flush(&midi);
    }

    class midi_decoder final : public decoder_base
    {
        midi_file midi_{nullptr, ss_midi_free};
        sequencer seq_{nullptr, ss_sequencer_free};
        processor proc_{nullptr, ss_processor_free};
        roland sc_{nullptr, emu88_free_context};
        std::unique_ptr<mu2000> mu_;
        std::vector<uint8_t> midi_bytes_, bank_bytes_;
        std::filesystem::path directory_;
        int backend_ = 0;
        unsigned port_ = 0;
        std::array<float, SS_MAX_SOUND_CHUNK * 2> block_{};
        size_t offset_ = SS_MAX_SOUND_CHUNK;

        static void command(void *context, const uint8_t *data, size_t count, double)
        {
            auto &self = *static_cast<midi_decoder *>(context);
            if (!count) return;
            if (data[0] == 0xf5 && count == 2)
            {
                self.port_ = data[1] ? data[1] - 1 : 0;
                return;
            }
            if (self.mu_)
            {
                if (self.port_ >= mu2000::MIDI_PORTS) return;
                for (size_t i = 0; i < count; ++i) self.mu_->midi_in(data[i], self.port_);
            }
            else if (self.sc_ && self.port_ < unsigned(emu88_get_midi_port_count(self.sc_.get())))
                emu88_parse_stream_on_port(self.sc_.get(), self.port_, data, uint32_t(count));
        }
        void release_synth()
        {
            seq_.reset(); proc_.reset(); mu_.reset(); sc_.reset();
            offset_ = SS_MAX_SOUND_CHUNK;
        }
        bool reset_synth()
        {
            backend_ = replayer_settings::snapshot()[replayer_settings::midi_backend];
            release_synth(); port_ = 0;
            if (backend_ == 0)
            {
                std::call_once(spessa_tables, ss_unit_converter_init);
                if (bank_bytes_.empty())
                    bank_bytes_ = resource_bytes(directory_ / "detonate.sf2", 512ull * 1024 * 1024);
                ss_file file(ss_file_open_from_memory(bank_bytes_.data(), bank_bytes_.size(), false), ss_file_close);
                soundbank bank(file ? ss_soundbank_load(file.get()) : nullptr, ss_soundbank_free);
                if (!bank) throw std::runtime_error("Unable to load " + utf8(directory_ / "detonate.sf2"));
                proc_.reset(ss_processor_create(rate_, nullptr));
                if (!proc_ || !ss_processor_load_soundbank(proc_.get(), bank.get(), "detonate", 0, false))
                    throw std::runtime_error("Unable to initialize SpessaSynth.");
                bank.release(); // Ownership transfers on successful registration.
                seq_.reset(ss_sequencer_create(proc_.get()));
            }
            else if (backend_ == 1)
            {
                mu_ = std::make_unique<mu2000>();
                auto program = resource_bytes(directory_ / "mu2000_flash.bin", 4 * 1024 * 1024);
                if (!mu_->load_program_data(program.data(), program.size()))
                    throw std::runtime_error("Invalid MU2000 program ROM: " + mu_->error());
                std::array<std::vector<uint8_t>, 4> waves;
                const uint8_t *parts[4]; size_t sizes[4];
                for (unsigned i = 0; i < 4; ++i)
                {
                    waves[i] = resource_bytes(directory_ / mu2000::WAVE_ROM_NAMES[i], 8 * 1024 * 1024);
                    parts[i] = waves[i].data(); sizes[i] = waves[i].size();
                }
                if (!mu_->load_wave_data(parts, sizes))
                    throw std::runtime_error("Invalid MU2000 wave ROMs: " + mu_->error());
                std::error_code ec;
                const auto table_path = directory_ / "sin-table.bin";
                if (std::filesystem::exists(table_path, ec))
                {
                    auto table = resource_bytes(table_path, 65536);
                    if (!mu_->load_sintab_data(table.data(), table.size()))
                        throw std::runtime_error("Invalid MU2000 sine table.");
                }
                else
                {
                    // The same analytic stand-in used by upstream's ROM tools.
                    auto table = std::make_shared<std::vector<uint16_t>>(32768);
                    for (size_t i = 0; i < table->size(); ++i)
                        (*table)[i] = uint16_t(std::min(65535.0, std::round(32768 + std::sin((i + 0.5) / 32768 * 1.5707963267948966) * 32767)));
                    mu_->set_sintab_rom(std::move(table));
                }
                mu_->set_fast_midi(true); mu_->reset();
                for (unsigned i = 0; i < rate_ * 30 && !mu_->midi_ready(); ++i)
                {
                    int32_t l, r; mu_->run_sample(l, r);
                }
                if (!mu_->midi_ready()) throw std::runtime_error("MU2000 firmware did not finish booting.");
            }
            else
            {
                // 88emu's ROM catalogue and search paths are process-wide.
                std::lock_guard lock(roland_mutex);
                const int device = backend_ == 2 ? EMU88_DEVICE_SC55 : backend_ == 3 ? EMU88_DEVICE_SC55MK2 : EMU88_DEVICE_SC8850;
                if (emu88_set_rom_path(utf8(directory_).c_str()) != EMU88_RC_OK || !emu88_is_device_available(device))
                {
                    std::array<char, 4096> description{};
                    emu88_describe_device_roms(device, description.data(), description.size());
                    throw std::runtime_error("Missing MIDI ROMs in " + utf8(directory_) + ": " + description.data());
                }
                sc_.reset(emu88_create_context());
                if (!sc_ || emu88_select_device(sc_.get(), device) != EMU88_RC_OK)
                    throw std::runtime_error("Unable to initialize 88emu.");
                emu88_set_stereo_output_samplerate(sc_.get(), rate_);
                emu88_set_boot_flags(sc_.get(), EMU88_BOOT_DEFAULT);
                if (emu88_open_synth(sc_.get()) != EMU88_RC_OK)
                    throw std::runtime_error("Unable to boot 88emu with ROMs in " + utf8(directory_));
            }
            if (!proc_)
            {
                SS_SequencerCallbacks callbacks{rate_, command, nullptr, this};
                seq_.reset(ss_sequencer_create_callbacks(&callbacks));
            }
            if (!seq_) throw std::runtime_error("Unable to initialize the MIDI sequencer.");
            ss_sequencer_set_skip_to_first_note_on(seq_.get(), false);
            ss_sequencer_set_loop_count(seq_.get(), loop_ && midi_->last_voice_event_tick ? -1 : 0);
            if (!ss_sequencer_load_midi(seq_.get(), midi_.get())) return false;
            ss_sequencer_play(seq_.get());
            return true;
        }
        void render_block()
        {
            ss_sequencer_tick(seq_.get(), SS_MAX_SOUND_CHUNK);
            if (proc_) ss_processor_render_interleaved(proc_.get(), block_.data(), SS_MAX_SOUND_CHUNK);
            else if (mu_)
                for (unsigned i = 0; i < SS_MAX_SOUND_CHUNK; ++i)
                {
                    int32_t l, r; mu_->run_sample(l, r);
                    block_[i * 2] = float(l) / mu2000::DAC_FULL_SCALE;
                    block_[i * 2 + 1] = float(r) / mu2000::DAC_FULL_SCALE;
                }
            else emu88_render_float(sc_.get(), block_.data(), SS_MAX_SOUND_CHUNK);
            offset_ = 0;
        }
        size_t read_frames(float *out, size_t count) override
        {
            if (!seq_ || !midi_) return 0;
            const bool bounded = !loop_ || !midi_->last_voice_event_tick;
            if (bounded && position_ >= length_) return 0;
            if (bounded) count = std::min<uint64_t>(count, length_ - position_);
            if (offset_ == SS_MAX_SOUND_CHUNK) render_block();
            count = std::min(count, SS_MAX_SOUND_CHUNK - offset_);
            std::copy_n(block_.data() + offset_ * 2, count * 2, out);
            offset_ += count;
            return count;
        }
        bool seek_frame(uint64_t frame) override
        {
            if (!midi_ || !reset_synth()) return false;
            // Rebuild voices, controllers and firmware state by rendering the lead-in.
            while (frame)
            {
                render_block();
                const auto skip = std::min<uint64_t>(frame, SS_MAX_SOUND_CHUNK);
                offset_ = size_t(skip); frame -= skip;
            }
            return true;
        }
    public:
        ~midi_decoder() override { stop(); }
        bool open(const char *name, float *rate, bool loop) override
        {
            if (!name || !rate) return false;
            std::error_code ec;
            auto size = std::filesystem::file_size(std::filesystem::path(reinterpret_cast<const char8_t *>(name)), ec);
            if (ec || size > 16 * 1024 * 1024) return false;
            return open_memory(name, read_audio_file(name), rate, loop);
        }
        bool open_memory(const std::string &name, const std::vector<uint8_t> &bytes, float *rate, bool loop) override
        {
            stop();
            if (!rate || bytes.empty() || bytes.size() > 16 * 1024 * 1024 || !valid_midi(bytes)) return false;
            midi_bytes_ = bytes;
            ss_file file(ss_file_open_from_memory(midi_bytes_.data(), midi_bytes_.size(), false), ss_file_close);
            midi_.reset(file ? ss_midi_load(file.get(), name.c_str()) : nullptr);
            if (midi_) normalize_smpte(*midi_);
            if (!midi_ || !midi_->track_count || !midi_->time_division ||
                !std::isfinite(midi_->duration) || midi_->duration < 0 || midi_->duration > 86400) return false;
            directory_ = midi_resource_directory();
            backend_ = replayer_settings::snapshot()[replayer_settings::midi_backend];
            rate_ = 44100; channels_ = 2; loop_ = loop;
            length_ = uint64_t(std::ceil((midi_->duration + 3.0) * rate_));
            if (midi_->binary_name) metadata_.title.assign(reinterpret_cast<const char *>(midi_->binary_name), midi_->binary_name_length);
            if (midi_->rmidi_info.artist) metadata_.artist.assign(reinterpret_cast<const char *>(midi_->rmidi_info.artist), midi_->rmidi_info.artist_len);
            if (midi_->rmidi_info.album) metadata_.album.assign(reinterpret_cast<const char *>(midi_->rmidi_info.album), midi_->rmidi_info.album_len);
            if (!reset_synth()) { stop(); return false; }
            playing_ = true; *rate = float(rate_);
            return true;
        }
        void stop() override
        {
            release_synth(); midi_.reset();
            midi_bytes_.clear(); bank_bytes_.clear(); metadata_.clear();
            playing_ = false; rate_ = channels_ = 0; position_ = length_ = 0;
        }
        bool supports_looping() const override { return true; }
        void set_loop(bool loop) override
        {
            decoder_base::set_loop(loop);
            if (seq_) ss_sequencer_set_loop_count(seq_.get(), loop && midi_->last_voice_event_tick ? -1 : 0);
        }
        std::vector<std::string> file_types() override { return {"mid", "midi", "rmi", "kar"}; }
    };
}
void midi_set_system_directory(const std::filesystem::path &directory)
{
    std::lock_guard lock(resources_mutex);
    resources = directory / "detonatemidi";
}
std::filesystem::path midi_resource_directory()
{
    std::lock_guard lock(resources_mutex);
    return resources;
}
auddecode *create_midi() { return new midi_decoder; }
