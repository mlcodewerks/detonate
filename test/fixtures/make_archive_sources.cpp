// Generates original synthetic tones; no game assets or external inputs.
#include "../game_music_fixtures.h"
int main()
{
    using namespace game_fixtures;
    std::filesystem::create_directories("folder");
    save("folder/01.spc", spc("First"));
    save("02.spc", spc("Second", 0x800));
    save("folder/sample.vgz", gzip(vgm()));
    save("folder/nested.rsn", rsn({{"inside.spc", spc("Nested")}}));
    save("notes.txt", {'h', 'i'});
}
