/* Native libretro host. No CRT startup, runtime library, SDL or C++ runtime.
 * Keep state in zero-initialized storage and use only Windows-owned services. */
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <mmsystem.h>
#include "libretro.h"

#define CORE_API(X)                                                         \
    X(retro_api_version)                                                    \
    X(retro_init)                                                           \
    X(retro_deinit) X(retro_run)                                            \
        X(retro_load_game) X(retro_unload_game) X(retro_get_system_av_info) \
            X(retro_set_environment) X(retro_set_video_refresh)             \
                X(retro_set_audio_sample_batch) X(retro_set_input_poll) X(retro_set_input_state)
#define DECLARE(name) static __typeof__(&name) core_##name;
CORE_API(DECLARE)
#undef DECLARE
static bool(RETRO_CALLCONV *core_load_async)(const struct retro_game_info *);
static int(RETRO_CALLCONV *core_load_status)(void);

enum
{
    AUDIO_SLOTS = 16,
    PATH_CHARS = 32768
};
typedef struct pointer_event
{
    struct pointer_event *next;
    int x, y, button;
    BOOL down;
} pointer_event;
static HMODULE library;
static HWND window;
static HDC memory_dc;
static HBITMAP bitmap;
static HGDIOBJ old_bitmap;
static BYTE *framebuffer;
static HWAVEOUT audio;
static WAVEHDR audio_headers[AUDIO_SLOTS];
static unsigned audio_capacity, audio_slot, audio_frames, video_frames;
static struct retro_system_av_info av;
static struct retro_keyboard_callback keyboard;
static BOOL initialized, loaded, running, hidden, fullscreen, keys[RETROK_LAST];
static BOOL buttons[3], timer_started, releasing_capture;
static RECT viewport;
static WINDOWPLACEMENT saved_placement;
static LONG saved_style;
static int mouse_x, mouse_y, mouse_dx, mouse_dy, mouse_wheel, pending_wheel;
static pointer_event *mouse_head, *mouse_tail;
static WCHAR pending_file[PATH_CHARS], library_path[PATH_CHARS];
static WORD high_surrogate;
static const WCHAR *failure;

static void *allocate(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size); }
static void discard(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

static void copy_bytes(void *to, const void *from, SIZE_T count)
{
    volatile BYTE *d = to;
    const volatile BYTE *s = from;
    while (count--)
        *d++ = *s++;
}
static void report(const WCHAR *message)
{
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written;
    char *utf8;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, message, -1, NULL, 0, NULL, NULL);
    utf8 = allocate(bytes);
    if (utf8)
    {
        WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, bytes, NULL, NULL);
        WriteFile(err, utf8, bytes - 1, &written, NULL);
        WriteFile(err, "\r\n", 2, &written, NULL);
        discard(utf8);
    }
    if (!hidden)
        MessageBoxW(window, message, L"Detonate", MB_OK | MB_ICONERROR);
}
static void release_input(void)
{
    unsigned i;
    while (mouse_head)
    {
        pointer_event *next = mouse_head->next;
        discard(mouse_head);
        mouse_head = next;
    }
    mouse_tail = NULL;
    for (i = 0; i < 3; ++i)
        buttons[i] = FALSE;
    mouse_dx = mouse_dy = mouse_wheel = pending_wheel = 0;
    high_surrogate = 0;
    for (i = 0; i < RETROK_LAST; ++i)
    {
        if (keys[i] && keyboard.callback)
            keyboard.callback(false, i, 0, 0);
        keys[i] = FALSE;
    }
}
static void resize(void)
{
    RECT client;
    int w, h, vw, vh;
    GetClientRect(window, &client);
    w = client.right;
    h = client.bottom;
    vw = w;
    vh = MulDiv(w, av.geometry.base_height, av.geometry.base_width);
    if (vh > h)
    {
        vh = h;
        vw = MulDiv(h, av.geometry.base_width, av.geometry.base_height);
    }
    viewport.left = (w - vw) / 2;
    viewport.top = (h - vh) / 2;
    viewport.right = viewport.left + vw;
    viewport.bottom = viewport.top + vh;
}
static void pointer(int x, int y, int button, BOOL down)
{
    pointer_event *event;
    int w = viewport.right - viewport.left, h = viewport.bottom - viewport.top;
    if (w <= 0 || h <= 0)
        return;
    if (button >= 0 && down && (x < viewport.left || y < viewport.top || x >= viewport.right || y >= viewport.bottom))
        return;
    x = MulDiv(x - viewport.left, av.geometry.base_width, w);
    y = MulDiv(y - viewport.top, av.geometry.base_height, h);
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= (int)av.geometry.base_width)
        x = av.geometry.base_width - 1;
    if (y >= (int)av.geometry.base_height)
        y = av.geometry.base_height - 1;
    if (button < 0 && mouse_tail && mouse_tail->button < 0)
        event = mouse_tail;
    else
    {
        event = allocate(sizeof(*event));
        if (!event)
        {
            failure = L"Out of memory queuing input.";
            return;
        }
        if (mouse_tail)
            mouse_tail->next = event;
        else
            mouse_head = event;
        mouse_tail = event;
    }
    event->x = x;
    event->y = y;
    event->button = button;
    event->down = down;
}
static void sync_pointer(void)
{
    POINT point;
    if (GetCursorPos(&point) && ScreenToClient(window, &point))
        pointer(point.x, point.y, -1, FALSE);
}
static void end_capture(void)
{
    releasing_capture = TRUE;
    ReleaseCapture();
    releasing_capture = FALSE;
}
static void RETRO_CALLCONV poll(void)
{
    int x = mouse_x, y = mouse_y;
    while (mouse_head)
    {
        pointer_event *event = mouse_head;
        int button = event->button;
        mouse_x = event->x;
        mouse_y = event->y;
        if (button >= 0)
            buttons[button] = event->down;
        mouse_head = event->next;
        discard(event);
        if (!mouse_head)
            mouse_tail = NULL;
        if (button >= 0)
            break; /* Preserve clicks shorter than one frame. */
    }
    mouse_dx = mouse_x - x;
    mouse_dy = mouse_y - y;
    mouse_wheel = pending_wheel / WHEEL_DELTA;
    if (mouse_wheel > 32)
        mouse_wheel = 32;
    if (mouse_wheel < -32)
        mouse_wheel = -32;
    pending_wheel -= mouse_wheel * WHEEL_DELTA;
}
static int16_t RETRO_CALLCONV input(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port || device != RETRO_DEVICE_MOUSE)
        return 0;
    switch (id)
    {
    case RETRO_DEVICE_ID_MOUSE_X:
        return (int16_t)mouse_dx;
    case RETRO_DEVICE_ID_MOUSE_Y:
        return (int16_t)mouse_dy;
    case RETRO_DEVICE_ID_MOUSE_LEFT:
        return buttons[0];
    case RETRO_DEVICE_ID_MOUSE_RIGHT:
        return buttons[1];
    case RETRO_DEVICE_ID_MOUSE_MIDDLE:
        return buttons[2];
    case RETRO_DEVICE_ID_MOUSE_WHEELUP:
        return mouse_wheel > 0 ? (int16_t)mouse_wheel : 0;
    case RETRO_DEVICE_ID_MOUSE_WHEELDOWN:
        return mouse_wheel < 0 ? (int16_t)-mouse_wheel : 0;
    default:
        return 0;
    }
}
static unsigned key_code(WPARAM key, LPARAM bits)
{
    BOOL extended = (bits & (1L << 24)) != 0;
    if (key >= 'A' && key <= 'Z')
        return (unsigned)key + ('a' - 'A');
    if (key >= '0' && key <= '9')
        return (unsigned)key;
    if (key >= VK_F1 && key <= VK_F12)
        return RETROK_F1 + (unsigned)key - VK_F1;
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
        return RETROK_KP0 + (unsigned)key - VK_NUMPAD0;
    if (!extended)
    {
        switch (key)
        { /* Num Lock off: preserve the keypad's identity. */
        case VK_INSERT:
            return RETROK_KP0;
        case VK_END:
            return RETROK_KP1;
        case VK_DOWN:
            return RETROK_KP2;
        case VK_NEXT:
            return RETROK_KP3;
        case VK_LEFT:
            return RETROK_KP4;
        case VK_CLEAR:
            return RETROK_KP5;
        case VK_RIGHT:
            return RETROK_KP6;
        case VK_HOME:
            return RETROK_KP7;
        case VK_UP:
            return RETROK_KP8;
        case VK_PRIOR:
            return RETROK_KP9;
        case VK_DELETE:
            return RETROK_KP_PERIOD;
        }
    }
    switch (key)
    {
    case VK_SHIFT:
        return MapVirtualKeyW((bits >> 16) & 255, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? RETROK_RSHIFT : RETROK_LSHIFT;
    case VK_CONTROL:
        return extended ? RETROK_RCTRL : RETROK_LCTRL;
    case VK_MENU:
        return extended ? RETROK_RALT : RETROK_LALT;
    case VK_RETURN:
        return extended ? RETROK_KP_ENTER : RETROK_RETURN;
#define KEY(win, retro) \
    case VK_##win:      \
        return RETROK_##retro
        KEY(BACK, BACKSPACE);
        KEY(TAB, TAB);
        KEY(CLEAR, CLEAR);
        KEY(PAUSE, PAUSE);
        KEY(ESCAPE, ESCAPE);
        KEY(SPACE, SPACE);
        KEY(DELETE, DELETE);
        KEY(LEFT, LEFT);
        KEY(RIGHT, RIGHT);
        KEY(UP, UP);
        KEY(DOWN, DOWN);
        KEY(HOME, HOME);
        KEY(END, END);
        KEY(PRIOR, PAGEUP);
        KEY(NEXT, PAGEDOWN);
        KEY(INSERT, INSERT);
        KEY(CAPITAL, CAPSLOCK);
        KEY(SCROLL, SCROLLOCK);
        KEY(NUMLOCK, NUMLOCK);
        KEY(SNAPSHOT, PRINT);
        KEY(LWIN, LSUPER);
        KEY(RWIN, RSUPER);
        KEY(APPS, MENU);
        KEY(DECIMAL, KP_PERIOD);
        KEY(DIVIDE, KP_DIVIDE);
        KEY(MULTIPLY, KP_MULTIPLY);
        KEY(SUBTRACT, KP_MINUS);
        KEY(ADD, KP_PLUS);
#undef KEY
    default:
    {
        unsigned character = MapVirtualKeyW((UINT)key, MAPVK_VK_TO_CHAR) & 0x7fffffff;
        return character < 128 ? character : RETROK_UNKNOWN;
    }
    }
}
static uint16_t modifiers(void)
{
    return ((GetKeyState(VK_SHIFT) < 0) ? RETROKMOD_SHIFT : 0) |
           ((GetKeyState(VK_CONTROL) < 0) ? RETROKMOD_CTRL : 0) |
           ((GetKeyState(VK_MENU) < 0) ? RETROKMOD_ALT : 0) |
           ((GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) ? RETROKMOD_META : 0) |
           ((GetKeyState(VK_CAPITAL) & 1) ? RETROKMOD_CAPSLOCK : 0) |
           ((GetKeyState(VK_NUMLOCK) & 1) ? RETROKMOD_NUMLOCK : 0);
}
static void text_character(unsigned character)
{
    if (keyboard.callback && character >= 32 && character != 127)
        keyboard.callback(true, RETROK_UNKNOWN, character, modifiers());
}
static void toggle_fullscreen(void)
{
    fullscreen = !fullscreen;
    if (fullscreen)
    {
        MONITORINFO monitor = {sizeof(monitor)};
        saved_placement.length = sizeof(saved_placement);
        GetWindowPlacement(window, &saved_placement);
        saved_style = GetWindowLongW(window, GWL_STYLE);
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
    }
    else
    {
        SetWindowLongW(window, GWL_STYLE, saved_style);
        SetWindowPlacement(window, &saved_placement);
        SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE);
    }
}
static void clear_letterbox(HDC dc, const RECT *client)
{
    int saved = SaveDC(dc);
    if (!saved)
    {
        failure = L"Unable to save video clipping state.";
        return;
    }
    /* Never erase pixels covered by a frame. Otherwise the intermediate black
     * window is visible between GDI operations on every presentation. */
    if (bitmap)
        ExcludeClipRect(dc, viewport.left, viewport.top, viewport.right, viewport.bottom);
    FillRect(dc, client, GetStockObject(BLACK_BRUSH));
    RestoreDC(dc, saved);
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message)
    {
    case WM_CLOSE:
        running = FALSE;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        RECT client;
        HDC dc = BeginPaint(hwnd, &paint);
        GetClientRect(hwnd, &client);
        clear_letterbox(dc, &client);
        if (bitmap)
        {
            SetStretchBltMode(dc, HALFTONE);
            SetBrushOrgEx(dc, 0, 0, NULL);
            if (!StretchBlt(dc, viewport.left, viewport.top, viewport.right - viewport.left, viewport.bottom - viewport.top,
                            memory_dc, 0, 0, av.geometry.base_width, av.geometry.base_height, SRCCOPY) &&
                !IsIconic(hwnd))
                failure = L"Unable to present video.";
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_SIZE:
        if (window)
        {
            resize();
            sync_pointer();
        }
        return 0;
    case WM_KILLFOCUS:
        release_input();
        end_capture();
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        return 0;
    case WM_SETFOCUS:
        if (window)
            sync_pointer();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && GetFocus() == hwnd)
        {
            SetCursor(NULL);
            return TRUE;
        }
        break;
    case WM_CAPTURECHANGED:
        if (!releasing_capture)
            release_input();
        return 0;
    case WM_MOUSEMOVE:
        pointer(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), -1, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    {
        BOOL down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
        int button = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ? 0 : message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ? 1
                                                                                                                                     : 2;
        if (down)
            SetCapture(hwnd);
        pointer(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), button, down);
        /* An intentional release must preserve the queued press/release pair. */
        if (!down && !(wp & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
            end_capture();
        return 0;
    }
    case WM_MOUSEWHEEL:
        pending_wheel += GET_WHEEL_DELTA_WPARAM(wp);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        BOOL down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        unsigned key;
        if (down && (lp & (1L << 30)))
            return 0;
        if (wp == VK_F11)
        {
            if (down)
                toggle_fullscreen();
            return 0;
        }
        if (down && wp == 'Q' && GetKeyState(VK_CONTROL) < 0)
        {
            running = FALSE;
            return 0;
        }
        if (wp == VK_F4 && (lp & (1L << 29)))
            break;
        key = key_code(wp, lp);
        if (key && key < RETROK_LAST)
        {
            keys[key] = down;
            if (keyboard.callback)
                keyboard.callback(down != FALSE, key, 0, modifiers());
        }
        return 0;
    }
    case WM_CHAR:
        if (wp >= 0xd800 && wp <= 0xdbff)
        {
            high_surrogate = (WORD)wp;
            return 0;
        }
        if (wp >= 0xdc00 && wp <= 0xdfff)
        {
            if (high_surrogate)
                text_character(0x10000 + ((high_surrogate - 0xd800) << 10) + (unsigned)wp - 0xdc00);
        }
        else
            text_character((unsigned)wp);
        high_surrogate = 0;
        return 0;
    case WM_UNICHAR:
        if (wp == UNICODE_NOCHAR)
            return TRUE;
        if (wp <= 0x10ffff && !(wp >= 0xd800 && wp <= 0xdfff))
            text_character((unsigned)wp);
        return 0;
    case WM_DROPFILES:
        DragQueryFileW((HDROP)wp, 0, pending_file, PATH_CHARS);
        DragFinish((HDROP)wp);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
static bool RETRO_CALLCONV environment(unsigned command, void *data)
{
    switch (command)
    {
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
        keyboard.callback = ((struct retro_keyboard_callback *)data)->callback;
        return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:
        running = FALSE;
        return true;
    default:
        return false;
    }
}
static void RETRO_CALLCONV video(const void *pixels, unsigned width, unsigned height, size_t pitch)
{
    unsigned row;
    if (!pixels)
        return;
    if (pixels == RETRO_HW_FRAME_BUFFER_VALID || width != av.geometry.base_width || height != av.geometry.base_height ||
        pitch < width * 4 || pitch > 0x7fffffffU || pitch > (SIZE_T)-1 / height)
    {
        failure = L"Unsupported core video frame.";
        return;
    }
    GdiFlush(); /* Finish pending GDI reads before writing the DIB's memory. */
    for (row = 0; row < height; ++row)
        copy_bytes(framebuffer + row * width * 4, (const BYTE *)pixels + row * pitch, width * 4);
    ++video_frames;
}
static size_t RETRO_CALLCONV samples(const int16_t *pcm, size_t frames)
{
    unsigned i, queued = 0;
    WAVEHDR *header = &audio_headers[audio_slot];
    for (i = 0; i < AUDIO_SLOTS; ++i)
    {
        WAVEHDR *item = &audio_headers[i];
        if ((item->dwFlags & WHDR_PREPARED) && !(item->dwFlags & WHDR_DONE))
            queued += item->dwBufferLength / 4;
    }
    if ((header->dwFlags & WHDR_PREPARED) && !(header->dwFlags & WHDR_DONE))
        return 0;
    if (frames > audio_capacity - queued)
        frames = audio_capacity - queued;
    if (!frames)
        return 0;
    if (header->dwFlags & WHDR_PREPARED)
    {
        if (waveOutUnprepareHeader(audio, header, sizeof(*header)) != MMSYSERR_NOERROR)
        {
            failure = L"Unable to reclaim audio buffer.";
            return 0;
        }
    }
    copy_bytes(header->lpData, pcm, frames * 4);
    header->dwBufferLength = (DWORD)frames * 4;
    header->dwFlags = 0;
    if (waveOutPrepareHeader(audio, header, sizeof(*header)) != MMSYSERR_NOERROR ||
        waveOutWrite(audio, header, sizeof(*header)) != MMSYSERR_NOERROR)
    {
        failure = L"Unable to queue audio.";
        return 0;
    }
    audio_slot = (audio_slot + 1) % AUDIO_SLOTS;
    audio_frames += (unsigned)frames;
    return frames;
}
static BOOL load(const WCHAR *path)
{
    struct retro_game_info game = {0};
    BOOL opened;
    char *utf8 = NULL;
    release_input();
    end_capture();
    if (loaded && !core_load_async)
        core_retro_unload_game();
    loaded = FALSE;
    if (waveOutReset(audio) != MMSYSERR_NOERROR)
    {
        failure = L"Unable to clear audio.";
        return FALSE;
    }
    mouse_x = av.geometry.base_width / 2;
    mouse_y = av.geometry.base_height / 2;
    if (path && *path)
    {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
        if (!bytes || !(utf8 = allocate(bytes)))
        {
            failure = L"Unable to convert content path.";
            return FALSE;
        }
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, bytes, NULL, NULL);
        game.path = utf8;
    }
    loaded = core_load_async ? core_load_async(utf8 ? &game : NULL) : core_retro_load_game(utf8 ? &game : NULL);
    discard(utf8);
    opened = loaded;
    if (!loaded)
        loaded = core_retro_load_game(NULL);
    if (!loaded)
        failure = L"Unable to initialize the Detonate browser.";
    if (opened && path && *path)
    {
        WCHAR *title = allocate((lstrlenW(path) + 12) * sizeof(WCHAR));
        if (title)
        {
            lstrcpyW(title, L"Detonate - ");
            lstrcatW(title, path);
            SetWindowTextW(window, title);
            discard(title);
        }
    }
    else
        SetWindowTextW(window, L"Detonate");
    sync_pointer();
    return opened;
}
static BOOL start(void)
{
    WNDCLASSW cls = {0};
    BITMAPINFO info = {0};
    WAVEFORMATEX format = {0};
    RECT rect;
    unsigned i;
    library = LoadLibraryW(library_path);
    if (!library)
    {
        failure = L"Unable to load Detonate core DLL (check path and architecture).";
        return FALSE;
    }
#define LOAD(name)                                                           \
    core_##name = (__typeof__(&name))(void *)GetProcAddress(library, #name); \
    if (!core_##name)                                                        \
    {                                                                        \
        failure = L"Missing required libretro export.";                      \
        return FALSE;                                                        \
    }
    CORE_API(LOAD)
#undef LOAD
    core_load_async = (void *)GetProcAddress(library, "detonate_load_game_async");
    core_load_status = (void *)GetProcAddress(library, "detonate_load_status");
    if (!core_load_status)
        core_load_async = NULL;
    if (core_retro_api_version() != RETRO_API_VERSION)
    {
        failure = L"Unsupported core API version.";
        return FALSE;
    }
    core_retro_set_environment(environment);
    core_retro_set_video_refresh(video);
    core_retro_set_audio_sample_batch(samples);
    core_retro_set_input_poll(poll);
    core_retro_set_input_state(input);
    core_retro_init();
    initialized = TRUE;
    core_retro_get_system_av_info(&av);
    if (!av.geometry.base_width || av.geometry.base_width > 4096 || !av.geometry.base_height || av.geometry.base_height > 4096 ||
        !(av.timing.fps >= 1 && av.timing.fps <= 240) || !(av.timing.sample_rate >= 8000 && av.timing.sample_rate <= 384000))
    {
        failure = L"Unsupported core geometry or audio timing.";
        return FALSE;
    }
    SetProcessDPIAware();
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.lpszClassName = L"Detonate";
    rect.left = rect.top = 0;
    rect.right = av.geometry.base_width;
    rect.bottom = av.geometry.base_height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    if (!RegisterClassW(&cls))
    {
        failure = L"Unable to register window.";
        return FALSE;
    }
    window = CreateWindowExW(WS_EX_ACCEPTFILES, cls.lpszClassName, L"Detonate", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, cls.hInstance, NULL);
    if (!window)
    {
        failure = L"Unable to create window.";
        return FALSE;
    }
    resize();
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = av.geometry.base_width;
    info.bmiHeader.biHeight = -(LONG)av.geometry.base_height; /* Top-down XRGB8888. */
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    memory_dc = CreateCompatibleDC(NULL);
    bitmap = CreateDIBSection(memory_dc, &info, DIB_RGB_COLORS, (void **)&framebuffer, NULL, 0);
    if (!memory_dc || !bitmap)
    {
        failure = L"Unable to create video buffer.";
        return FALSE;
    }
    old_bitmap = SelectObject(memory_dc, bitmap);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = (DWORD)av.timing.sample_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = format.nSamplesPerSec * 4;
    if (waveOutOpen(&audio, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        failure = L"Unable to open WinMM audio device.";
        return FALSE;
    }
    audio_capacity = format.nSamplesPerSec / 10; /* At most 100 ms in flight. */
    for (i = 0; i < AUDIO_SLOTS; ++i)
    {
        audio_headers[i].lpData = allocate(audio_capacity * 4);
        if (!audio_headers[i].lpData)
        {
            failure = L"Out of memory allocating audio.";
            return FALSE;
        }
    }
    timer_started = timeBeginPeriod(1) == TIMERR_NOERROR;
    if (!hidden)
    {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }
    return TRUE;
}
static void cleanup(void)
{
    unsigned i;
    release_input();
    if (loaded)
        core_retro_unload_game();
    if (initialized)
        core_retro_deinit();
    if (audio)
    {
        waveOutReset(audio);
        for (i = 0; i < AUDIO_SLOTS; ++i)
        {
            if (audio_headers[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(audio, &audio_headers[i], sizeof(WAVEHDR));
            discard(audio_headers[i].lpData);
        }
        waveOutClose(audio);
    }
    if (timer_started)
        timeEndPeriod(1);
    if (old_bitmap)
        SelectObject(memory_dc, old_bitmap);
    if (bitmap)
        DeleteObject(bitmap);
    if (memory_dc)
        DeleteDC(memory_dc);
    if (window)
        DestroyWindow(window);
    if (library)
        FreeLibrary(library);
}
/* Explicit PE entry point: the OS supplies a zeroed BSS; no CRT initializers. */
void __cdecl detonate_entry(void)
{
    int argc = 0, i;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const WCHAR *content = NULL;
    unsigned frame_limit = 0, frames = 0, ready_frames = 0;
    BOOL initial_load = TRUE;
    LARGE_INTEGER frequency, counter;
    double deadline, interval;
    running = TRUE;
    if (!argv)
        failure = L"Unable to parse command line.";
    /* Suppress modal errors even when an earlier argument is invalid. */
    for (i = 1; i < argc; ++i)
        if (!lstrcmpW(argv[i], L"--hidden"))
            hidden = TRUE;
    for (i = 1; i < argc && !failure; ++i)
    {
        const WCHAR *arg = argv[i];
        if (!lstrcmpW(arg, L"--help") || !lstrcmpW(arg, L"-h"))
        {
            report(L"Usage: detonate [--core PATH] [FILE]\nDrop a file to open it. F11: fullscreen. Ctrl+Q: quit.\n--frames N --hidden: bounded smoke runs (requires an audio device).");
            LocalFree(argv);
            ExitProcess(0);
        }
        else if (!lstrcmpW(arg, L"--core"))
        {
            if (++i == argc || !*argv[i])
                failure = L"Missing core path.";
            else
                lstrcpynW(library_path, argv[i], PATH_CHARS);
        }
        else if (!lstrcmpW(arg, L"--frames"))
        {
            const WCHAR *p;
            if (++i == argc)
            {
                failure = L"Missing frame count.";
                break;
            }
            p = argv[i];
            frame_limit = 0;
            while (*p >= '0' && *p <= '9' && frame_limit <= 1000000)
                frame_limit = frame_limit * 10 + *p++ - '0';
            if (*p || !frame_limit || frame_limit > 1000000)
                failure = L"Invalid frame count.";
        }
        else if (!lstrcmpW(arg, L"--hidden"))
            hidden = TRUE;
        else if (arg[0] == '-' && arg[1] == '-')
            failure = L"Unknown command-line option.";
        else if (content)
            failure = L"Only one content file may be specified.";
        else
            content = arg;
    }
    if (!failure && !*library_path)
    {
        DWORD length = GetModuleFileNameW(NULL, library_path, PATH_CHARS);
        if (!length || length >= PATH_CHARS)
            failure = L"Unable to locate executable.";
        else
        {
            while (length && library_path[length - 1] != '\\' && library_path[length - 1] != '/')
                --length;
            if (length + 22 >= PATH_CHARS)
                failure = L"Core path is too long.";
            else
                lstrcpyW(library_path + length, L"detonate-libretro.dll");
        }
    }
    if (!failure && start() && !load(content) && !failure)
        failure = L"Unable to open content file.";
    if (!failure && (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter)))
        failure = L"Unable to initialize frame clock.";
    if (!failure)
    {
        interval = (double)frequency.QuadPart / av.timing.fps;
        deadline = (double)counter.QuadPart;
        while (running && (!frame_limit || ready_frames < frame_limit) && !failure)
        {
            MSG message;
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    running = FALSE;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running)
                break;
            if (*pending_file)
            {
                initial_load = FALSE;
                if (!load(pending_file) && !failure)
                    report(L"Unable to open dropped file.");
                *pending_file = 0;
            }
            if (failure)
                break;
            core_retro_run();
            ++frames;
            if (core_load_async)
            {
                int status = core_load_status();
                if (status < 0)
                {
                    if (initial_load)
                        failure = L"Unable to open content file.";
                    /* Later load failures remain visible in the core's browser. */
                }
                if (status != 0)
                {
                    ++ready_frames;
                    initial_load = FALSE;
                }
            }
            else
                ++ready_frames;
            if (!hidden)
            {
                InvalidateRect(window, NULL, FALSE);
                UpdateWindow(window);
            }
            deadline += interval;
            for (;;)
            {
                double remaining;
                QueryPerformanceCounter(&counter);
                remaining = deadline - (double)counter.QuadPart;
                if (remaining <= 0)
                    break;
                Sleep((DWORD)(remaining * 1000.0 / (double)frequency.QuadPart) + 1);
            }
            if ((double)counter.QuadPart - deadline > interval)
                deadline = (double)counter.QuadPart;
        }
        if (frame_limit && !failure && (video_frames != frames || !audio_frames))
            failure = L"Core did not deliver video and audio.";
    }
    if (failure)
        report(failure);
    cleanup();
    if (argv)
        LocalFree(argv);
    ExitProcess(failure ? 1 : 0);
}
