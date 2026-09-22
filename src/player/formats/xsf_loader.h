#pragma once
#include "replay_engine.h"
#include <map>

struct xsf_section
{
    std::vector<uint8_t> exe, reserved;
};
struct xsf_file
{
    uint8_t version = 0;
    std::vector<xsf_section> sections;
    std::map<std::string, std::string> tags;
};
xsf_file load_xsf(const char *path);
std::vector<uint8_t> inflate_xsf(const uint8_t *bytes, size_t size);
bool xsf_map(std::vector<uint8_t> &target, const uint8_t *data, size_t size,
             size_t limit, bool power_of_two = false);
struct xsf_engine : replay_engine
{
    xsf_file file;
    virtual bool accepts(uint8_t version) const = 0;
    bool load(const char *path) override;
    int tag_number(const char *name, int fallback = 0) const;
};
std::unique_ptr<replay_engine> make_psx_engine();
std::unique_ptr<replay_engine> make_sega_engine();
std::unique_ptr<replay_engine> make_qsf_engine();
std::unique_ptr<replay_engine> make_usf_engine();
std::unique_ptr<replay_engine> make_gsf_engine();
std::unique_ptr<replay_engine> make_snsf_engine();
std::unique_ptr<replay_engine> make_2sf_engine();
