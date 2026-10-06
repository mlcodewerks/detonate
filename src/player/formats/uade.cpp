#include "../replayer_settings.h"
#include "replay_engine.h"
#include "replays/UADE/extensions.h"
#include <chrono>
#include <thread>
#include <atomic>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <fcntl.h>
#endif

namespace
{
    struct helper_process
    {
#ifdef _WIN32
        HANDLE process = nullptr, input = nullptr, output = nullptr, read_event = nullptr;
#else
        pid_t process = -1;
        int input = -1, output = -1;
#endif
        ~helper_process() { close(); }
        void close()
        {
#ifdef _WIN32
            if (input) CloseHandle(input);
            if (output) CloseHandle(output);
            if (read_event) CloseHandle(read_event);
            if (process) { TerminateProcess(process,0); WaitForSingleObject(process,5000); CloseHandle(process); }
            process = input = output = read_event = nullptr;
#else
            if (input >= 0) ::close(input);
            if (output >= 0) ::close(output);
            if (process > 0) { kill(process,SIGKILL); while (waitpid(process,nullptr,0) < 0 && errno == EINTR) {} }
            input = output = -1; process = -1;
#endif
        }
        bool start()
        {
            close();
            std::filesystem::path location;
#ifdef _WIN32
            HMODULE module;
            wchar_t path[32768];
            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&create_uade), &module) && GetModuleFileNameW(module,path,32768))
                location = std::filesystem::path(path).parent_path() / "detonate-uade.exe";
#else
            Dl_info info{};
            if (dladdr(reinterpret_cast<void *>(&create_uade), &info))
                location = std::filesystem::path(info.dli_fname).parent_path() / "detonate-uade";
#endif
            std::error_code ec;
            if (!std::filesystem::exists(location,ec)) location = std::filesystem::u8path(DETONATE_UADE_HELPER);
#ifdef _WIN32
            SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
            HANDLE child_in = nullptr, child_out = nullptr;
            if (!CreatePipe(&child_in,&input,&security,0)) return false;
            static std::atomic<unsigned> serial{0};
            auto pipe_name = L"\\\\.\\pipe\\detonate-uade-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(serial++);
            output = CreateNamedPipeW(pipe_name.c_str(),PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,1,8192,8192,0,nullptr);
            if (output == INVALID_HANDLE_VALUE) { output = nullptr; CloseHandle(child_in); close(); return false; }
            child_out = CreateFileW(pipe_name.c_str(),GENERIC_WRITE,0,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (child_out == INVALID_HANDLE_VALUE) { CloseHandle(child_in); close(); return false; }
            read_event = CreateEventW(nullptr,TRUE,FALSE,nullptr);
            OVERLAPPED connection{}; connection.hEvent = read_event;
            if (!read_event || (!ConnectNamedPipe(output,&connection) && GetLastError() != ERROR_PIPE_CONNECTED)) {
                CloseHandle(child_in); CloseHandle(child_out); close(); return false;
            }
            SetHandleInformation(input,HANDLE_FLAG_INHERIT,0); SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = child_in; startup.hStdOutput = child_out;
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            PROCESS_INFORMATION pi{};
            bool ok = CreateProcessW(location.c_str(),nullptr,nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi);
            CloseHandle(child_in); CloseHandle(child_out);
            if (!ok) { close(); return false; }
            process = pi.hProcess; CloseHandle(pi.hThread);
#else
            int in[2], out[2];
            if (socketpair(AF_UNIX,SOCK_STREAM,0,in)) return false;
            if (socketpair(AF_UNIX,SOCK_STREAM,0,out)) { ::close(in[0]); ::close(in[1]); return false; }
            for (int fd : {in[0],in[1],out[0],out[1]}) fcntl(fd,F_SETFD,FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
            int enabled = 1;
            setsockopt(in[1],SOL_SOCKET,SO_NOSIGPIPE,&enabled,sizeof(enabled));
#endif
            process = fork();
            if (!process) {
                dup2(in[0],STDIN_FILENO); dup2(out[1],STDOUT_FILENO);
                ::close(in[0]); ::close(in[1]); ::close(out[0]); ::close(out[1]);
                execl(location.c_str(),location.c_str(),nullptr); _exit(127);
            }
            ::close(in[0]); ::close(out[1]); input = in[1]; output = out[0];
            if (process < 0) { close(); return false; }
#endif
            return true;
        }
        bool send(const void *src, size_t size)
        {
            const auto *p = static_cast<const uint8_t *>(src);
            while (size) {
#ifdef _WIN32
                DWORD n;
                if (!input || !WriteFile(input,p,DWORD(size),&n,nullptr) || !n) return false;
#else
#ifdef MSG_NOSIGNAL
                auto n = ::send(input,p,size,MSG_NOSIGNAL);
#else
                auto n = ::send(input,p,size,0);
#endif
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) return false;
#endif
                p += n; size -= n;
            }
            return true;
        }
        bool receive(void *dst, size_t size)
        {
            auto *p = static_cast<uint8_t *>(dst);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (size) {
                if (std::chrono::steady_clock::now() >= deadline) { close(); return false; }
#ifdef _WIN32
                DWORD n = 0;
                if (!output || !read_event) return false;
                ResetEvent(read_event);
                OVERLAPPED operation{}; operation.hEvent = read_event;
                if (!ReadFile(output,p,DWORD(size),&n,&operation)) {
                    if (GetLastError() != ERROR_IO_PENDING) return false;
                    HANDLE waits[] = {read_event,process};
                    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
                    DWORD status = WaitForMultipleObjects(2,waits,FALSE,DWORD(std::max<int64_t>(0,remaining)));
                    if (status != WAIT_OBJECT_0) {
                        CancelIoEx(output,&operation);
                        GetOverlappedResult(output,&operation,&n,TRUE);
                        close(); return false;
                    }
                    if (!GetOverlappedResult(output,&operation,&n,FALSE)) return false;
                }
                if (!n) return false;
#else
                pollfd fd{output,POLLIN,0};
                if (poll(&fd,1,100) < 0 && errno != EINTR) return false;
                if (!(fd.revents & (POLLIN|POLLHUP))) continue;
                auto n = ::read(output,p,size);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) return false;
#endif
                p += n; size -= n;
            }
            return true;
        }
    };
    struct uade_engine final : replay_engine
    {
        helper_process helper;
        std::string source_path;
        std::array<uint32_t, 3> applied{};
        static std::array<uint32_t, 3> settings()
        {
            const auto s = replayer_settings::snapshot();
            return {uint32_t(s[replayer_settings::uade_resampler]), uint32_t(s[replayer_settings::uade_filter]), uint32_t(s[replayer_settings::uade_led])};
        }
        bool load(const char *path) override
        {
            std::error_code ec;
            auto size = std::filesystem::file_size(std::filesystem::u8path(path),ec);
            if (ec || size < 8 || size > 16 * 1024 * 1024 || !helper.start()) return false;
            auto absolute = std::filesystem::absolute(std::filesystem::u8path(path)).u8string();
            source_path.assign(absolute.begin(), absolute.end());
            applied = settings();
            uint32_t n = uint32_t(absolute.size()), header[4];
            if (!helper.send(&n,4) || !helper.send(absolute.data(),n) || !helper.send(applied.data(),sizeof(applied)) || !helper.receive(header,sizeof(header)) ||
                header[0] != 0x55414445 || !header[1] || header[1] > 256 || header[2] >= header[1]) return false;
            tracks = header[1]; track = header[2]; duration = header[3];
            if (!helper.receive(&n,4) || n > 4096) return false;
            title.resize(n);
            return helper.receive(title.data(),n);
        }
        bool reset(unsigned i) override
        {
            if (settings() != applied)
            {
                const auto path = source_path;
                if (!load(path.c_str())) return false;
            }
            uint32_t command[2] = {1,i}, ok;
            if (i >= tracks || !helper.send(command,sizeof(command)) || !helper.receive(&ok,4) || !ok) return false;
            track = i;
            return true;
        }
        bool render(std::vector<float> &out) override
        {
            uint32_t command = 2, size;
            if (!helper.send(&command,4) || !helper.receive(&size,4) || !size || size > 4096 || size % 4) return false;
            std::array<int16_t,2048> pcm;
            if (!helper.receive(pcm.data(),size)) return false;
            replay_pcm16(out,pcm.data(),size / 4);
            return true;
        }
    };
}
auddecode *create_uade()
{
    return new replay_decoder(std::make_unique<uade_engine>(),uade_extensions());
}
