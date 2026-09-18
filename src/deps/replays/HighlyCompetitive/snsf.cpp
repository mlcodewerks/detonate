#include "snes9x/snes9x.h"
#include "snes9x/apu/apu.h"
#include "snes9x/apu/resampler.h"
#include "snes9x/memmap.h"

void S9xMessage(int type, int message_no, const char* str)
{
    (void)type;
    (void)message_no;
    (void)str;
}

bool8 S9xOpenSoundDevice(void)
{
    return TRUE;
}

bool8 pad_read = false;
static bool s_isInit = false;
static bool s_isRunning = false;

bool Snes9xInit(const uint8_t* rom, size_t romSize, const uint8_t* sram, size_t sramSize)
{
    // ROM Options
    memset(&Settings, 0, sizeof(Settings));

    // Tracing options
    Settings.TraceDMA = false;
    Settings.TraceHDMA = false;
    Settings.TraceVRAM = false;
    Settings.TraceUnknownRegisters = false;
    Settings.TraceDSP = false;

    // ROM timing options (see also	H_Max above)
    Settings.PAL = false;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.FrameTime = 16667;

    // CPU options
    Settings.Paused = false;

    Settings.TakeScreenshot = false;

    // Sound options
    Settings.SoundSync = true;
    Settings.Mute = false;
    Settings.SoundPlaybackRate = 48000;
    Settings.SixteenBitSound = true;
    Settings.Stereo = true;
    Settings.ReverseStereo = false;
    Settings.InterpolationMethod = 2;
    Settings.DisableSurround = false;

    Memory.Init();

    S9xInitAPU();
    S9xInitSound(10);

    s_isInit = true;
    s_isRunning = false;

    if (!Memory.LoadROMSNSF(rom, int32_t(romSize), sram, int32_t(sramSize))) { Memory.Deinit(); S9xDeinitAPU(); s_isInit = false; return false; }
    S9xSetSoundMute(false);
    return true;
}

void Snes9xRelease()
{
    if (s_isInit)
    {
        S9xReset();
        Memory.Deinit();
        S9xDeinitAPU();

        s_isInit = false;
    }
}

void Snes9xSetInterpolationMethod(int32_t interpostionMethod)
{
    Settings.InterpolationMethod = interpostionMethod;
}

bool Snes9xRender(int16_t* buf, uint32_t numSamples)
{
    unsigned idleFrames = 0;
    while (numSamples) {
        unsigned available = S9xGetSampleCount() / 2;
        if (!available) {
            if (++idleFrames > 600) return false;
            S9xSyncSound(); S9xMainLoop();
        } else {
            idleFrames = 0;
            unsigned n = available < numSamples ? available : numSamples;
            S9xMixSamples(reinterpret_cast<uint8_t*>(buf), n * 2);
            buf += n * 2; numSamples -= n;
        }
    }
    return true;
}
