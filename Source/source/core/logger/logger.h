#pragma once
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <string>
#include <format>
#include <memory>
#include <vector>

#include <sdk/offsets/offsets.h>
#include <core/memory/memory.h>
#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>

namespace Cheat::Console {

    enum class Color : unsigned short {
        Default   = 0x07,
        Dim       = 0x08,
        Gray      = 0x07,
        White     = 0x0F,
        Red       = 0x0C,
        Green     = 0x0A,
        Blue      = 0x09,
        Cyan      = 0x0B,
        Magenta   = 0x0D,
        Yellow    = 0x0E,
        Orange    = 0x06,
        Teal      = 0x03,
        Purple    = 0x05,
        Lime      = 0x02,
        Sky       = 0x0B,
        Pink      = 0x0D,
        BrightRed = 0x4C,
    };

    namespace detail {
        inline std::mutex g_mu;
        inline bool g_vt = false;
        inline bool g_have_last_crash = false;
        inline unsigned long g_last_crash_code = 0;

        inline void ensure_console()
        {
            static bool once = false;
            if (once)
                return;
            once = true;

            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (!h || h == INVALID_HANDLE_VALUE)
                return;

            DWORD mode = 0;
            if (GetConsoleMode(h, &mode))
            {
                mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                g_vt = SetConsoleMode(h, mode) != 0;
            }
        }

        inline void stamp(char* out, size_t n)
        {
            SYSTEMTIME st{};
            GetLocalTime(&st);
            std::snprintf(out, n, "[%02u:%02u:%02u]",
                (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond);
        }

        inline WORD attr(Color c)
        {
            return (WORD)c;
        }

        inline void write_colored(Color color, const char* text)
        {
            ensure_console();
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h && h != INVALID_HANDLE_VALUE)
            {
                CONSOLE_SCREEN_BUFFER_INFO csbi{};
                GetConsoleScreenBufferInfo(h, &csbi);
                SetConsoleTextAttribute(h, attr(color));
                std::fputs(text, stdout);
                SetConsoleTextAttribute(h, csbi.wAttributes);
            }
            else
            {
                std::fputs(text, stdout);
            }
        }

        inline void log_locked(Color color, const char* body)
        {
            char t[16]{};
            stamp(t, sizeof(t));
            write_colored(Color::Dim, t);
            write_colored(Color::Dim, ": ");
            write_colored(color, body);
            std::fputc('\n', stdout);
            std::fflush(stdout);
        }

        enum class CrashSide {
            User,
            Cheat,
            Both,
            Info
        };

        struct CrashInfo {
            const char* name;
            CrashSide side;
            const char* fix;
            const char* dev;
        };

        inline CrashInfo classify_crash(unsigned long code)
        {
            if (code == 0)
            {
                return {
                    "Clean Exit",
                    CrashSide::Info,
                    "clean exit.",
                    nullptr
                };
            }
            else if (code == 0xC000013A)
            {
                return {
                    "Process Terminated",
                    CrashSide::User,
                    "killed from taskmgr / another app. reopen.",
                    nullptr
                };
            }
            else if (code == 0xC0000135)
            {
                return {
                    "DLL Not Found",
                    CrashSide::User,
                    "reinstall roblox + vcredist.",
                    nullptr
                };
            }
            else if (code == 0xC0000141)
            {
                return {
                    "Invalid Address",
                    CrashSide::Cheat,
                    "cfg/bad call. try off raycast silent / lua.",
                    "raycast stub = in-module cave or xrw+cfg. luavm: call-rax;ret in-module."
                };
            }
            else if (code == 0xC0000142)
            {
                return {
                    "DLL Init Failed",
                    CrashSide::User,
                    "dll init fail. update gpu, vcredist, reinstall roblox.",
                    nullptr
                };
            }
            else if (code == 0xC000007B)
            {
                return {
                    "Bad Image",
                    CrashSide::User,
                    "bad image. delete %localappdata%\\Roblox + reinstall.",
                    nullptr
                };
            }
            else if (code == 0xC0000017 || code == 0xC000009A)
            {
                return {
                    "Out Of Memory",
                    CrashSide::User,
                    "oom. close apps, lower gfx, reboot.",
                    nullptr
                };
            }
            else if (code == 0xC0000006)
            {
                return {
                    "In-Page Error",
                    CrashSide::User,
                    "disk/ram read fail. check disk + memdiag.",
                    nullptr
                };
            }
            else if (code == 0xC0000185)
            {
                return {
                    "IO Device Error",
                    CrashSide::User,
                    "io device error. check disk, reinstall roblox.",
                    nullptr
                };
            }
            else if (code == 0xE06D7363)
            {
                return {
                    "C++ Exception",
                    CrashSide::User,
                    "c++ exception. reinstall roblox usually fixes.",
                    "if only with features on - note which."
                };
            }
            else if (code == 0xC0000005)
            {
                return {
                    "Access Violation",
                    CrashSide::Both,
                    "av. update gpu, disable overlays, reinstall if keeps happening.",
                    "often cheat: bad r/w or silent. off silent/fly/speed."
                };
            }
            else if (code == 0xC0000374)
            {
                return {
                    "Heap Corruption",
                    CrashSide::Cheat,
                    "heap corrupt. features off + still? reinstall roblox.",
                    "bad write (fly/speed/silent). bisect features."
                };
            }
            else if (code == 0xC0000409)
            {
                return {
                    "Stack Buffer Overrun",
                    CrashSide::Cheat,
                    "stack smash. features off? reinstall roblox.",
                    "usually silent inject. off silent, check stub."
                };
            }
            else if (code == 0xC00000FD)
            {
                return {
                    "Stack Overflow",
                    CrashSide::Cheat,
                    "stack overflow. reboot; clean still crash -> reinstall.",
                    "maybe hook recursion. check silent path."
                };
            }
            else if (code == 0xC000001D)
            {
                return {
                    "Illegal Instruction",
                    CrashSide::Both,
                    "illegal insn. reinstall roblox; update windows.",
                    "bad patched bytes in silent stub?"
                };
            }
            else if (code == 0xC0000096)
            {
                return {
                    "Privileged Instruction",
                    CrashSide::Cheat,
                    "priv insn. av inject? add exclusions.",
                    "bad patch / wrong rva."
                };
            }
            else if (code == 0xC000041D)
            {
                return {
                    "Fatal Callback Exception",
                    CrashSide::Both,
                    "fatal callback. reboot, gpu drivers, overlays off, reinstall.",
                    "hooks during window/input callbacks."
                };
            }

            if ((code & 0xF0000000u) == 0xC0000000u)
            {
                return {
                    "NTSTATUS Crash",
                    CrashSide::Both,
                    "unknown ntstatus. reinstall roblox + disable overlays.",
                    "log code + features on."
                };
            }

            return {
                "Unknown Exit",
                CrashSide::Both,
                "weird exit. reopen; reinstall if repeats.",
                "save exit code + features."
            };
        }

        inline const char* side_text(CrashSide side)
        {
            if (side == CrashSide::User)
                return "user (fixable)";
            else if (side == CrashSide::Cheat)
                return "cheat (dev)";
            else if (side == CrashSide::Both)
                return "user + cheat";
            else if (side == CrashSide::Info)
                return "info";
            return "unknown";
        }

        inline void dump_crash_locked(unsigned long exit_code)
        {
            CrashInfo info = classify_crash(exit_code);
            long long signed_code = (long long)(std::int32_t)exit_code;

            log_locked(Color::White, "Ivory crash");
            {
                char body[160]{};
                std::snprintf(body, sizeof(body), "Exit code      0x%08lX (%lld)",
                    exit_code, signed_code);
                log_locked(Color::Yellow, body);
            }
            {
                char body[160]{};
                std::snprintf(body, sizeof(body), "Name           %s", info.name);
                log_locked(Color::Orange, body);
            }
            {
                char body[160]{};
                std::snprintf(body, sizeof(body), "Side           %s", side_text(info.side));

                Color c = Color::Magenta;
                if (info.side == CrashSide::Cheat)
                    c = Color::Red;
                else if (info.side == CrashSide::User)
                    c = Color::Green;
                else if (info.side == CrashSide::Info)
                    c = Color::Gray;

                log_locked(c, body);
            }
            if (info.fix && info.fix[0])
            {
                char body[640]{};
                std::snprintf(body, sizeof(body), "Fix            %s", info.fix);
                log_locked(Color::Lime, body);
            }
            if (info.dev && info.dev[0])
            {
                char body[640]{};
                std::snprintf(body, sizeof(body), "Dev            %s", info.dev);
                log_locked(Color::Cyan, body);
            }
        }
    } // namespace detail

    inline void Clear()
    {
        std::lock_guard<std::mutex> lock(detail::g_mu);
        detail::ensure_console();
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!h || h == INVALID_HANDLE_VALUE)
            return;

        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (!GetConsoleScreenBufferInfo(h, &csbi))
            return;

        const DWORD cells = (DWORD)csbi.dwSize.X * (DWORD)csbi.dwSize.Y;
        DWORD written = 0;
        COORD home{ 0, 0 };
        FillConsoleOutputCharacterA(h, ' ', cells, home, &written);
        FillConsoleOutputAttribute(h, csbi.wAttributes, cells, home, &written);
        SetConsoleCursorPosition(h, home);
    }

    inline void Log(Color color, const char* fmt, ...)
    {
        if (!fmt)
            return;

        char body[640]{};
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(body, sizeof(body), fmt, ap);
        va_end(ap);

        std::lock_guard<std::mutex> lock(detail::g_mu);
        detail::log_locked(color, body);
    }

    inline void Log(const char* fmt, ...)
    {
        if (!fmt)
            return;

        char body[640]{};
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(body, sizeof(body), fmt, ap);
        va_end(ap);

        std::lock_guard<std::mutex> lock(detail::g_mu);
        detail::log_locked(Color::White, body);
    }

    inline void Ptr(Color color, const char* name, std::uint64_t addr)
    {
        Log(color, "%-14s 0x%llX", name ? name : "?", (unsigned long long)addr);
    }

    inline void Ptr(const char* name, std::uint64_t addr)
    {
        Ptr(Color::Cyan, name, addr);
    }

    inline void DumpCrash(unsigned long exit_code)
    {
        std::lock_guard<std::mutex> lock(detail::g_mu);
        detail::g_have_last_crash = true;
        detail::g_last_crash_code = exit_code;
        detail::dump_crash_locked(exit_code);
    }

    inline void DumpLastCrash()
    {
        std::lock_guard<std::mutex> lock(detail::g_mu);
        if (!detail::g_have_last_crash)
            return;
        detail::dump_crash_locked(detail::g_last_crash_code);
    }

    inline void DumpWorld()
    {
        uintptr_t base = memory ? memory->m_base_address : 0;
        DWORD pid = memory ? (DWORD)memory->get_pid() : 0;

        std::uint64_t ve = (game::visualengine) ? game::visualengine->address : 0;
        std::uint64_t dm = (game::datamodel) ? game::datamodel->address : 0;
        std::uint64_t front = 0;
        if (base && memory)
            front = memory->read<std::uint64_t>(base + Offsets::FakeDataModel::Pointer);

        std::uint64_t ws = 0;
        std::uint64_t pl = 0;
        if (dm && memory)
        {
            ws = memory->read<std::uint64_t>(dm + Offsets::DataModel::Workspace);
            pl = game::datamodel->find_first_child_by_class("Players");
            if (!pl)
                pl = game::datamodel->find_first_child("Players");
        }

        cache::entity_t lp = cache::get_local_player();
        std::uint64_t local_player = lp.instance.address;
        if (!local_player && pl && memory)
        {
            local_player = memory->read<std::uint64_t>(pl + Offsets::Player::LocalPlayer);
        }

        std::uint64_t local_char = cache::local_character.address;
        if (!local_char && local_player && memory)
        {
            local_char = memory->read<std::uint64_t>(local_player + Offsets::Player::ModelInstance);
        }

        std::uint64_t camera = game::camera;
        if (!camera && ws && memory)
        {
            camera = memory->read<std::uint64_t>(ws + Offsets::Workspace::CurrentCamera);
        }

        std::int64_t place_id = 0;
        std::int64_t game_id = 0;
        std::int64_t user_id = (std::int64_t)lp.user_id;
        if (!user_id && local_player && memory)
        {
            user_id = memory->read<std::int64_t>(local_player + Offsets::Player::UserId);
        }
        std::uint64_t world = 0;

        if (dm && memory)
        {
            place_id = memory->read<std::int64_t>(dm + Offsets::DataModel::PlaceId);
            game_id = memory->read<std::int64_t>(dm + Offsets::DataModel::GameId);
        }
        if (ws && memory)
        {
            world = memory->read<std::uint64_t>(ws + Offsets::Workspace::World);
        }

        std::size_t players = 0;
        if (pl)
        {
            auto pl_children = rbx::c_instance(pl).get_children<rbx::c_instance>();
            players = pl_children.size();
        }
        if (players == 0)
        {
            players = cache::players.size() + (local_player ? 1 : 0);
        }

        Log(Color::Gray, "PID            %lu", (unsigned long)pid);
        Ptr(Color::Cyan, "Module", base);
        Ptr(Color::Yellow, "Front DM", front);
        Ptr(Color::Green, "Data Model", dm);
        Ptr(Color::Magenta, "VisualEngine", ve);
        Ptr(Color::Blue, "Workspace", ws);
        Ptr(Color::Teal, "World", world);
        Ptr(Color::Sky, "Players", pl);
        Ptr(Color::Lime, "LocalPlayer", local_player);
        Ptr(Color::Purple, "Character", local_char);
        Ptr(Color::Pink, "Camera", camera);
        Log(Color::Orange, "PlaceId        %lld", (long long)place_id);
        Log(Color::Yellow, "GameId         %lld", (long long)game_id);
        Log(Color::Lime, "UserId         %lld", (long long)user_id);
        Log(Color::White, "Cached         %zu players", players);

        bool have_crash = false;
        unsigned long crash_code = 0;
        {
            std::lock_guard<std::mutex> lock(detail::g_mu);
            have_crash = detail::g_have_last_crash;
            crash_code = detail::g_last_crash_code;
        }
        if (have_crash)
        {
            detail::CrashInfo info = detail::classify_crash(crash_code);
            Log(Color::Red, "Last crash     0x%08lX  %s  [%s]",
                crash_code, info.name, detail::side_text(info.side));
        }
    }

    inline void DumpSilent(bool ok, std::uint64_t handler, std::uint64_t stub,
                    std::uint64_t state, std::uint64_t slot, const char* detail_msg)
    {
        if (ok)
        {
            Log(Color::Green, "Silent inject  ok");
            Ptr(Color::Magenta, "Handler", handler);
            Ptr(Color::Yellow, "Stub", stub);
            Ptr(Color::Cyan, "State", state);
            Ptr(Color::Blue, "Slot", slot);
            Ptr(Color::Teal, "Desc RVA", Offsets::WorldRoot::RaycastBoundDesc);
            Log(Color::Gray, "Fn off         0x%llX",
                (unsigned long long)Offsets::WorldRoot::RaycastBoundFn);
        }
        else
        {
            Log(Color::Red, "Silent inject  fail%s%s",
                detail_msg && detail_msg[0] ? " " : "",
                detail_msg ? detail_msg : "");
            if (handler)
                Ptr(Color::Magenta, "Handler", handler);
            if (stub)
                Ptr(Color::Yellow, "Stub", stub);
            if (slot)
                Ptr(Color::Blue, "Slot", slot);
        }
    }

    inline void DumpGate(bool ok, const char* method, std::uint64_t slot,
                  std::uint64_t handler, std::uint64_t stub,
                  std::uint64_t state, bool cave, int fail)
    {
        if (!ok)
        {
            Log(Color::Red, "Gate install   fail (%d)", fail);
            return;
        }

        Log(Color::Green, "Gate install   ok");
        Log(Color::White, "Method         %s", method ? method : "?");
        Ptr(Color::Blue, "Slot", slot);
        Ptr(Color::Magenta, "Handler", handler);
        Ptr(Color::Yellow, "Stub", stub);
        Ptr(Color::Cyan, "State", state);
        Log(Color::Gray, "Stub place     %s", cave ? "cave" : "rwx");
    }

    inline void GateTimeout(const char* method, std::uint64_t calls)
    {
        Log(Color::Red, "Gate timeout   %s, hits %llu",
            method ? method : "?", (unsigned long long)calls);
    }

} // namespace Cheat::Console

// Backward compatibility bridge for existing code calling logger->log<level>()
enum e_level
{
    INFO,
    WARN,
    ERR,
    DEBUG, 
};

class c_logger final
{
public:
    c_logger() = default;

    template <e_level level, class... types>
    inline void log(const std::format_string<types...> format, types&&... args)
    {
        Cheat::Console::Color color = Cheat::Console::Color::White;
        if constexpr (level == e_level::INFO) { 
            color = Cheat::Console::Color::Green;
        }
        else if constexpr (level == e_level::WARN) { 
            color = Cheat::Console::Color::Orange;
        }
        else if constexpr (level == e_level::ERR) { 
            color = Cheat::Console::Color::Red;
        }
        else if constexpr (level == e_level::DEBUG) { 
            color = Cheat::Console::Color::Cyan;
        }

        std::string formatted_msg = std::format(format, std::forward<types>(args)...);
        Cheat::Console::Log(color, "%s", formatted_msg.c_str());
    }

    template <e_level level, class... types>
    inline void log_inline(const std::format_string<types...> format, types&&... args)
    {
        if constexpr (level == e_level::INFO || level == e_level::DEBUG) {
            return;
        }
        log<level>(format, std::forward<types>(args)...);
    }
};

inline std::unique_ptr<c_logger> logger = std::make_unique<c_logger>();