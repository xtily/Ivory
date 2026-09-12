#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <sdk/offsets/offsets.h>
#include <core/memory/memory.h>
#include <sdk/game/game.h>
#include <sdk/sdk.h>
#include <sdk/cache/core/cache.h>
#include <core/logger/logger.h>

extern std::unique_ptr<c_memory> memory;

#undef LOG_INFO
#undef LOG_ERROR
#undef LOG_SUCCESS
#undef LOG_WARNING

#include <cstdio>

#define LOG_INFO(fmt, ...)    do { printf("[Inherently-INFO] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_ERROR(fmt, ...)   do { printf("[Inherently-ERROR] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_SUCCESS(fmt, ...) do { printf("[Inherently-SUCCESS] " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_WARNING(fmt, ...) do { printf("[Inherently-WARN] " fmt "\n", ##__VA_ARGS__); } while(0)


namespace Mem {
    struct MemWrapper {
        uint64_t Base = 0;

        MemWrapper() {
            if (memory) Base = memory->m_base_address;
        }

        template <typename T>
        T Read(uint64_t addr) {
            if (!memory || addr < 0x10000 || addr > 0x7FFFFFFFFFFFull) return T{};
            return memory->read<T>(addr);
        }

        template <typename T>
        void Write(uint64_t addr, T val) {
            if (memory && addr >= 0x10000 && addr <= 0x7FFFFFFFFFFFull) {
                memory->write<T>(addr, val);
            }
        }

        bool ReadMemory(uint64_t addr, void* buf, size_t size) {
            if (!memory || !memory->m_process_handle || addr < 0x10000 || addr > 0x7FFFFFFFFFFFull) return false;
            SIZE_T read = 0;
            return ReadProcessMemory(memory->m_process_handle, (LPCVOID)addr, buf, size, &read) && read == size;
        }

        bool IsValid(uint64_t addr) {
            return addr >= 0x10000 && addr <= 0x7FFFFFFFFFFFull;
        }

        bool IsAlive() {
            return memory && memory->m_process_handle != nullptr;
        }

        bool IsAlive(uint64_t addr) {
            return IsValid(addr);
        }

        HANDLE GetHandle() {
            return memory ? memory->m_process_handle : nullptr;
        }

        uint64_t Allocate(size_t size) {
            if (!memory || !memory->m_process_handle) return 0;
            LPVOID p = VirtualAllocEx(memory->m_process_handle, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            return (uint64_t)p;
        }

        std::string ReadString(uint64_t addr) {
            if (!IsValid(addr)) return "";
            return memory->read_string(addr);
        }

        std::string GetInstanceName(uint64_t addr) {
            if (!IsValid(addr)) return "Unknown";
            return rbx::c_nameable(addr).get_name();
        }

        std::string GetClassName(uint64_t addr) {
            if (!IsValid(addr)) return "Unknown";
            return rbx::c_nameable(addr).get_class_name();
        }

        std::vector<uint64_t> GetChildren(uint64_t addr) {
            if (!IsValid(addr)) return {};
            return rbx::c_instance(addr).get_children();
        }

        uint64_t FindChild(uint64_t parent, std::string_view name) {
            if (!IsValid(parent)) return 0;
            return rbx::c_instance(parent).find_first_child(name);
        }
    };

    inline MemWrapper Get() { return MemWrapper{}; }
}

namespace Rbx {
    struct ServicesDataStruct {
        uint64_t PlayersService = 0;
        uint64_t Workspace = 0;
        uint64_t WorkspaceService = 0;
        uint64_t Lighting = 0;
    };

    struct RbxWrapper {
        uint64_t Base = 0;
        uint64_t DataModel = 0;
        uint64_t VisualEngine = 0;
        ServicesDataStruct ServicesData;

        void Refresh() {
            if (memory) Base = memory->m_base_address;
            if (game::datamodel) DataModel = game::datamodel->address;
            if (game::visualengine) VisualEngine = game::visualengine->address;
            if (cache::get_local_player().instance.is_valid()) {
                ServicesData.PlayersService = cache::get_local_player().instance.get_parent();
                ServicesData.WorkspaceService = cache::get_local_player().instance.get_parent();
                ServicesData.Workspace = ServicesData.WorkspaceService;
            }
        }
    };

    inline RbxWrapper Get() {
        RbxWrapper r;
        r.Refresh();
        return r;
    }
}