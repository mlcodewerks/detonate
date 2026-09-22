#include "xsf_loader.h"
#include <mutex>
#include "replays/HighlyExperimental/core/psx.h"
#include "replays/HighlyExperimental/core/bios.h"
#include "replays/HighlyExperimental/core/iop.h"
#include "replays/HighlyExperimental/core/r3000.h"
#include "replays/HighlyExperimental/psflib/psf2fs.h"
#include "replays/HighlyExperimental/hebios.h"
namespace
{
    struct psx_engine final : xsf_engine
    {
        std::vector<uint8_t> state;
        std::unique_ptr<void, decltype(&psf2fs_delete)> fs{nullptr, psf2fs_delete};
        bool accepts(uint8_t v) const override { return v == 1 || v == 2; }
        bool reset(unsigned) override
        {
            static std::once_flag once;
            std::call_once(once, []
                           { bios_set_image(hebios, HEBIOS_SIZE); psx_init(); });
            rate = file.version == 2 ? 48000 : 44100;
            state.resize(psx_get_state_size(file.version));
            psx_clear_state(state.data(), file.version);
            fs.reset(file.version == 2 ? psf2fs_create() : nullptr);
            bool first = true;
            int refresh = tag_number("_refresh");
            for (auto &s : file.sections)
            {
                if (file.version == 2)
                {
                    if (!fs || psf2fs_load_callback(fs.get(), s.exe.data(), s.exe.size(), s.reserved.data(), s.reserved.size()))
                        return false;
                }
                else if (!s.exe.empty())
                {
                    if (s.exe.size() < 0x800 || std::memcmp(s.exe.data(), "PS-X EXE", 8))
                        return false;
                    uint32_t addr = replay_le32(s.exe.data() + 0x18) & 0x1fffff;
                    size_t size = s.exe.size() - 0x800;
                    if (addr < 0x10000 || addr > 0x200000 || size > 0x200000 - addr)
                        return false;
                    void *iop = psx_get_iop_state(state.data());
                    iop_upload_to_ram(iop, addr, s.exe.data() + 0x800, uint32_t(size));
                    if (first)
                    {
                        void *cpu = iop_get_r3000_state(iop);
                        r3000_setreg(cpu, R3000_REG_PC, replay_le32(s.exe.data() + 0x10));
                        r3000_setreg(cpu, R3000_REG_GEN + 29, replay_le32(s.exe.data() + 0x30));
                        first = false;
                    }
                    if (!refresh)
                        refresh = std::memcmp(s.exe.data() + 113, "Europe", 6) == 0 ? 50 : 60;
                }
            }
            if (file.version == 1 && first)
                return false;
            if (refresh == 50 || refresh == 60)
                psx_set_refresh(state.data(), refresh);
            if (fs)
                psx_set_readfile(state.data(), psf2fs_virtual_readfile, fs.get());
            iop_set_compat(psx_get_iop_state(state.data()), IOP_COMPAT_HARSH);
            return true;
        }
        bool load(const char *path) override
        {
            if (!xsf_engine::load(path))
                return false;
            rate = file.version == 2 ? 48000 : 44100;
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            std::array<int16_t, 1024> pcm{};
            uint32_t n = 512;
            if (psx_execute(state.data(), 0x7fffffff, pcm.data(), &n, 0) < 0 || !n)
                return false;
            replay_pcm16(out, pcm.data(), n);
            return true;
        }
    };
}
std::unique_ptr<replay_engine> make_psx_engine() { return std::make_unique<psx_engine>(); }
