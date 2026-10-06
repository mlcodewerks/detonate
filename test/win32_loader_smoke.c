/* Exercise the native host's input, window and framebuffer boundary without
 * needing a core DLL or audio device. Tests may use the CRT; the host does not. */
#include "standalone/win32_loader.c"
#include <stdio.h>
#include <stdlib.h>

static unsigned text_value, text_count, releases;
static void check(BOOL ok, const char *what)
{
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); exit(1); }
}
static void RETRO_CALLCONV on_key(bool down, unsigned key, uint32_t character, uint16_t mod)
{
    (void)mod;
    if (key == RETROK_UNKNOWN && character) { text_value = character; ++text_count; }
    if (!down) ++releases;
}
static void check_letterbox(void)
{
    BITMAPINFO info = {0};
    void *pixels;
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old;
    RECT client = {0, 0, 40, 20};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 40; info.bmiHeader.biHeight = -20;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    check(dc && bitmap, "allocate paint target");
    old = SelectObject(dc, bitmap);
    FillRect(dc, &client, GetStockObject(WHITE_BRUSH));
    viewport = (RECT){10, 5, 30, 15};
    clear_letterbox(dc, &client);
    check(GetPixel(dc, 20, 10) == RGB(255, 255, 255), "background clear never flashes black over the frame");
    check(GetPixel(dc, 0, 10) == 0 && GetPixel(dc, 39, 10) == 0 &&
        GetPixel(dc, 20, 0) == 0 && GetPixel(dc, 20, 19) == 0, "all four letterbox bars clear");
    check(PtVisible(dc, 20, 10), "restore clipping before drawing the frame");
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); bitmap = NULL;
}
static void check_audio(void)
{
    WAVEFORMATEX format = {0};
    int16_t *pcm;
    unsigned i;
    format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 2;
    format.nSamplesPerSec = 44100; format.wBitsPerSample = 16;
    format.nBlockAlign = 4; format.nAvgBytesPerSec = 176400;
    check(waveOutOpen(&audio, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR, "open WinMM audio");
    audio_capacity = 4410;
    for (i = 0; i < AUDIO_SLOTS; ++i) {
        audio_headers[i].lpData = allocate(audio_capacity * 4);
        check(audio_headers[i].lpData != NULL, "allocate WinMM buffer");
    }
    pcm = allocate(5000 * 4); check(pcm != NULL, "allocate test PCM");
    check(waveOutPause(audio) == MMSYSERR_NOERROR, "pause device for deterministic queue test");
    check(samples(pcm, 5000) == 4410 && !failure, "oversized batch accepted only up to 100 ms");
    check(samples(pcm, 1) == 0 && !failure, "full queue backpressure");
    check(waveOutReset(audio) == MMSYSERR_NOERROR, "reset audio on content change");
    check(waveOutPause(audio) == MMSYSERR_NOERROR, "pause reset device");
    check(samples(pcm, 100) == 100 && !failure, "audio resumes after reset");
    for (i = 0; i < AUDIO_SLOTS - 1; ++i) check(samples(pcm, 100) == 100, "fill ring with short writes");
    check(samples(pcm, 100) == 0, "in-flight ring slot never overwritten");
    check(waveOutRestart(audio) == MMSYSERR_NOERROR, "resume playback");
    /* Device startup/completion latency varies, particularly with shared or
       virtual endpoints. Wait for completion instead of assuming 250 ms. */
    for (i = 0; i < 300 && !failure; ++i) {
        if (samples(pcm, 100) == 100) break;
        Sleep(10);
    }
    check(i < 300 && !failure, "completed header reused");
    discard(pcm); cleanup();
    puts("WinMM backpressure, reset and buffer reuse checks passed.");
}
int main(int argc, char **argv)
{
    WNDCLASSW cls = {0};
    BYTE pixels[2][12] = {{1, 2, 3, 0, 4, 5, 6, 0, 99, 99, 99, 99},
                          {7, 8, 9, 0, 10, 11, 12, 0, 99, 99, 99, 99}};
    BYTE output[16] = {0};
    (void)argv;
    if (argc > 1) { check_audio(); return 0; }
    check_letterbox();
    av.geometry.base_width = 1280; av.geometry.base_height = 720;
    cls.lpfnWndProc = window_proc; cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"DetonateInputTest";
    check(RegisterClassW(&cls), "register hidden test window");
    window = CreateWindowW(cls.lpszClassName, L"test", WS_POPUP, 0, 0, 1600, 1000,
        NULL, NULL, cls.hInstance, NULL);
    check(window != NULL, "create hidden test window");
    keyboard.callback = on_key;
    resize(); mouse_x = 640; mouse_y = 360;
    check(viewport.left == 0 && viewport.top == 50 && viewport.bottom == 950, "letterboxed viewport");
    pointer(800, 500, -1, FALSE); poll();
    check(!mouse_dx && !mouse_dy, "resized center maps to core center");
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(125, 300));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(125, 300));
    pointer(500, 550, -1, FALSE);
    poll();
    check(buttons[0] && mouse_x == 100 && mouse_y == 200, "quick click press and position survive capture release");
    poll(); check(!buttons[0] && !mouse_dx && !mouse_dy, "quick release arrives in next core frame");
    poll(); check(mouse_x == 400 && mouse_y == 400, "motion follows click");
    poll(); check(!mouse_dx && !mouse_dy, "relative deltas consumed once");
    pointer(800, 0, 0, TRUE); poll(); check(!buttons[0], "letterbox press ignored");
    pointer(125, 300, 0, TRUE); poll();
    pointer(-50, 0, 0, FALSE); poll(); check(!buttons[0] && !mouse_x && !mouse_y, "release outside ends drag");
    pending_wheel = 60; poll(); check(!mouse_wheel, "fractional wheel retained");
    pending_wheel += 60; poll(); check(mouse_wheel == 1, "fractional wheel accumulates");
    poll(); check(!mouse_wheel, "wheel consumed once");
    pending_wheel = -WHEEL_DELTA * 40; poll();
    check(input(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN) == 32, "wheel bounded per frame");
    poll(); check(mouse_wheel == -8, "wheel remainder preserved");
    keys[RETROK_a] = TRUE; pointer(125, 300, 0, TRUE); poll();
    SendMessageW(window, WM_KILLFOCUS, 0, 0); poll();
    check(!buttons[0] && !keys[RETROK_a] && releases == 1 && !mouse_head, "focus loss releases held input");
    check(key_code('V', 0) == RETROK_v && key_code(VK_F5, 0) == RETROK_F5 &&
        key_code(VK_RETURN, 1L << 24) == RETROK_KP_ENTER && key_code(VK_LEFT, 1L << 24) == RETROK_LEFT &&
        key_code(VK_CONTROL, 0) == RETROK_LCTRL && key_code(VK_CONTROL, 1L << 24) == RETROK_RCTRL &&
        key_code(VK_END, 0) == RETROK_KP1, "keyboard and keypad mappings");
    SendMessageW(window, WM_CHAR, 0x00e9, 0);
    check(text_value == 0xe9 && text_count == 1, "BMP Unicode text");
    SendMessageW(window, WM_CHAR, 0xd83d, 0); SendMessageW(window, WM_CHAR, 0xde80, 0);
    check(text_value == 0x1f680 && text_count == 2, "UTF-16 surrogate pair decoded once");
    SendMessageW(window, WM_CHAR, 1, 0); check(text_count == 2, "control shortcut is not text");
    SendMessageW(window, WM_CHAR, 0xd83d, 0); release_input();
    SendMessageW(window, WM_CHAR, 0xde80, 0); check(text_count == 2, "focus loss clears incomplete surrogate");
    check(SendMessageW(window, WM_UNICHAR, UNICODE_NOCHAR, 0), "Unicode capability probe");
    av.geometry.base_width = av.geometry.base_height = 2; framebuffer = output;
    video(pixels, 2, 2, 12);
    check(!failure && video_frames == 1 && output[0] == 1 && output[8] == 7 && output[14] == 12,
        "pitched top-down XRGB frame excludes row padding");
    video(NULL, 2, 2, 8); check(video_frames == 1, "duplicate frame retained");
    video(pixels, 2, 2, 4); check(failure != NULL, "short pitch rejected"); failure = NULL;
    video(RETRO_HW_FRAME_BUFFER_VALID, 2, 2, 8); check(failure != NULL, "hardware frame rejected"); failure = NULL;
    release_input(); DestroyWindow(window);
    puts("Native input, Unicode, letterboxing and framebuffer checks passed.");
    return 0;
}
