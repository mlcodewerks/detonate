#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <map>
#include <fstream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include <uade/uade.h>
#include <uade/uadeutils.h>
#undef fopen
#undef fread
#undef fwrite
#undef fclose
#undef fseek
#undef ftell
#undef fgets
#undef feof
static bool read(void *p, size_t n) { return std::fread(p,1,n,stdin) == n; }
static bool write(const void *p, size_t n) { return std::fwrite(p,1,n,stdout) == n; }
static void number(uint32_t n) { write(&n,4); }
static void text(const char *s) { auto n = uint32_t(std::strlen(s)); number(n); write(s,n); }
namespace {
std::filesystem::path song_directory;
std::map<std::string,std::vector<char>> companions;
std::string lower(std::string s)
{
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
struct uade_file *amiga_loader(const char *name, const char *playerdir, void *, uade_state *)
{
    auto *f = static_cast<struct uade_file *>(std::calloc(1,sizeof(struct uade_file)));
    if (!f) return nullptr;
    f->name = strdup(name);
    std::string request = name;
    auto colon = request.find(':');
    if (colon != std::string::npos && (lower(request.substr(0,colon)) == "env" || lower(request.substr(0,colon)) == "s")) {
        auto embedded = std::string(playerdir) + request;
        std::replace(embedded.begin(),embedded.end(),':','/');
        auto *extra = uade_file_load(embedded.c_str());
        uade_file_free(f);
        return extra;
    }
    if (!request.empty() && request.back() == '/') {
        f->data = static_cast<char *>(std::malloc(1)); return f;
    }
    auto found = companions.find(lower(request));
    if (found == companions.end()) {
        // Only files from the song's companion tree may be opened by Amiga code.
        std::replace(request.begin(),request.end(),'\\','/');
        auto colon = request.find(':');
        if (colon != std::string::npos) request.erase(0,colon+1);
        auto relative = std::filesystem::path(reinterpret_cast<const char8_t *>(request.c_str()));
        bool safe = !relative.is_absolute();
        for (const auto &part : relative) if (part == "..") safe = false;
        auto path = song_directory;
        std::error_code ec;
        if (safe) for (const auto &part : relative) {
            auto next = path / part;
            if (!std::filesystem::exists(next,ec)) {
                auto key = lower(part.string());
                for (const auto &entry : std::filesystem::directory_iterator(path,ec))
                    if (lower(entry.path().filename().string()) == key) { next = entry.path(); break; }
            }
            path = next;
        }
        auto size = safe ? std::filesystem::file_size(path,ec) : uintmax_t(-1);
        std::vector<char> bytes;
        if (safe && !ec && size <= 16 * 1024 * 1024) {
            std::ifstream input(path,std::ios::binary);
            bytes.resize(size);
            if (!input.read(bytes.data(),bytes.size())) bytes.clear();
        }
        if (bytes.empty()) {
            auto *extra = uade_file_load(("extras/"+request).c_str());
            if (!extra) {
                std::replace(request.begin(),request.end(),' ','_');
                extra = uade_file_load(("extras/"+request).c_str());
            }
            if (extra) {
                bytes.assign(extra->data,extra->data+extra->size);
                uade_file_free(extra);
            }
        }
        if (bytes.empty()) { uade_file_free(f); return nullptr; }
        found = companions.emplace(lower(name),std::move(bytes)).first;
    }
    f->size = found->second.size();
    f->data = static_cast<char *>(std::malloc(f->size));
    if (!f->data) { uade_file_free(f); return nullptr; }
    std::memcpy(f->data,found->second.data(),f->size);
    return f;
}
}
int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    uint32_t size;
    if (!read(&size,4) || !size || size > 32768) return 1;
    std::string filename(size, '\0');
    if (!read(filename.data(),size)) return 1;
    uint32_t settings[3];
    if (!read(settings, sizeof(settings)) || settings[0] > 2 || settings[1] > 2 || settings[2] > 2) return 1;
    auto *config = uade_new_config();
    if (!config) return 1;
    uade_config_set_option(config,UC_BASE_DIR,"");
    uade_config_set_option(config,UC_FREQUENCY,"44100");
    const char *resamplers[] = {"default", "sinc", "none"};
    uade_config_set_option(config,UC_RESAMPLER,resamplers[settings[0]]);
    if (settings[1] == 2) uade_config_set_option(config,UC_NO_FILTER,nullptr);
    else uade_config_set_option(config,UC_FILTER_TYPE,settings[1] ? "a1200" : "a500");
    if (settings[2]) uade_config_set_option(config,settings[2] == 1 ? UC_FORCE_LED_OFF : UC_FORCE_LED_ON,nullptr);
    uade_config_set_option(config,UC_ONE_SUBSONG,nullptr);
    uade_config_set_option(config,UC_NO_PANNING,nullptr);
    uade_config_set_option(config,UC_SILENCE_TIMEOUT_VALUE,"-1");
    uade_config_set_option(config,UC_SUBSONG_TIMEOUT_VALUE,"-1");
    auto *state = uade_new_state(config); std::free(config);
    auto *file = uade_file_load(filename.c_str());
    if (!state || !file || file->size > 16 * 1024 * 1024) return 1;
    auto path = std::filesystem::path(reinterpret_cast<const char8_t *>(filename.c_str()));
    song_directory = path.parent_path();
    std::vector<char> main(file->data,file->data+file->size);
    companions.emplace(lower(filename),main);
    auto base = path.filename().u8string();
    companions.emplace(lower(std::string(base.begin(),base.end())),std::move(main));
    uade_set_amiga_loader(amiga_loader,nullptr,state);
    if (uade_play_from_buffer(filename.c_str(),file->data,file->size,-1,state) != 1) return 1;
    const auto *info = uade_get_song_info(state);
    int first = info->subsongs.min;
    number(0x55414445); number(info->subsongs.max - first + 1);
    number(info->subsongs.cur - first);
    number(info->duration > 0 ? uint32_t(info->duration * 1000) : 180000);
    text(info->modulename); std::fflush(stdout);
    for (;;) {
        uint32_t command, track;
        if (!read(&command,4)) break;
        if (command == 1) {
            if (!read(&track,4)) break;
            uade_stop(state);
            int ok = uade_play_from_buffer(filename.c_str(),file->data,file->size,int(track)+first,state);
            number(ok == 1); std::fflush(stdout);
        } else if (command == 2) {
            int16_t pcm[2048];
            auto bytes = uade_read(pcm,sizeof(pcm),state);
            if (bytes == 0) {
                int song = uade_get_song_info(state)->subsongs.cur;
                uade_stop(state);
                if (uade_play_from_buffer(filename.c_str(),file->data,file->size,song,state) == 1)
                    bytes = uade_read(pcm,sizeof(pcm),state);
            }
            number(bytes > 0 ? uint32_t(bytes) : 0);
            if (bytes > 0) write(pcm,bytes);
            std::fflush(stdout);
        } else break;
    }
    // Exiting also tears down the core thread, which owns process-global UAE state.
    std::_Exit(0);
}
