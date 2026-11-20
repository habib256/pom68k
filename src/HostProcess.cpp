// POM68K — Macintosh 68k emulator
// VERHILLE Arnaud — Copyright (C) 2026 — GPLv3 (see LICENSE)

#include "HostProcess.h"

#include <chrono>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace pom68k::host {

std::string windowsCommandLine(const std::string& program,
                               const std::vector<std::string>& args) {
    // [B] Microsoft, "Parsing C command-line arguments": 2n backslashes
    // before a quote are n backslashes and a delimiter; 2n+1 are n and a
    // literal quote. Backslashes elsewhere are literal.
    auto quote = [](const std::string& a) {
        if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
        std::string out = "\"";
        std::size_t slashes = 0;
        for (char c : a) {
            if (c == '\\') { ++slashes; continue; }
            if (c == '"') out.append(slashes * 2 + 1, '\\');
            else out.append(slashes, '\\');
            slashes = 0;
            out += c;
        }
        out.append(slashes * 2, '\\');
        out += '"';
        return out;
    };
    std::string line = quote(program);
    for (const std::string& a : args) line += " " + quote(a);
    return line;
}

#if defined(__EMSCRIPTEN__)

bool hostProcessesAvailable() { return false; }

ProcessOutcome runHostProcess(const std::string&, const std::vector<std::string>&,
                              int, const std::function<bool()>&) {
    ProcessOutcome r;
    r.error = "helper programs are not available in the browser build";
    return r;
}

#elif defined(_WIN32)

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
} // namespace

bool hostProcessesAvailable() { return true; }

ProcessOutcome runHostProcess(const std::string& program,
                              const std::vector<std::string>& args,
                              int timeoutMs, const std::function<bool()>& stop) {
    ProcessOutcome r;
    std::wstring line = widen(windowsCommandLine(program, args));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    // Null application name: CreateProcessW searches the program on the
    // standard path (System32 holds curl.exe since Windows 10 1803).
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        const DWORD e = GetLastError();
        r.error = e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND
                      ? program + " was not found"
                      : program + " could not be started (error " + std::to_string(e) + ")";
        return r;
    }
    r.started = true;
    CloseHandle(pi.hThread);
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) break;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        const bool late = ms > timeoutMs;
        const bool halt = !late && stop && stop();
        if (late || halt) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            r.timedOut = late;
            r.stopped = halt;
            CloseHandle(pi.hProcess);
            return r;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = int(code);
    CloseHandle(pi.hProcess);
    return r;
}

#else // POSIX

bool hostProcessesAvailable() { return true; }

ProcessOutcome runHostProcess(const std::string& program,
                              const std::vector<std::string>& args,
                              int timeoutMs, const std::function<bool()>& stop) {
    ProcessOutcome r;
    std::vector<std::string> owned;
    owned.reserve(args.size() + 1);
    owned.push_back(program);
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : owned) argv.push_back(a.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, program.c_str(), &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        r.error = rc == ENOENT ? program + " was not found"
                               : program + " could not be started (" + std::strerror(rc) + ")";
        return r;
    }
    r.started = true;

    auto reap = [&](int& status) {
        for (;;) {
            const pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid) return true;
            if (w < 0 && errno != EINTR) return true;   // nothing left to wait for
            if (w == 0) return false;
        }
    };
    const auto t0 = std::chrono::steady_clock::now();
    int status = 0;
    for (;;) {
        if (reap(status)) break;
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0).count();
        const bool late = ms > timeoutMs;
        const bool halt = !late && stop && stop();
        if (late || halt) {
            ::kill(pid, SIGTERM);
            for (int i = 0; i < 20 && !reap(status); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            r.timedOut = late;
            r.stopped = halt;
            return r;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    if (WIFEXITED(status)) r.exitCode = WEXITSTATUS(status);
    else r.exitCode = 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return r;
}

#endif

} // namespace pom68k::host
