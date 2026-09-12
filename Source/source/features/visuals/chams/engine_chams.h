#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

#include <features/system/settings/settings.h>
#include <sdk/offsets/offsets.h>
#include <sdk/offsets/rva.h>
#include <core/memory/memory.h>

namespace hacks::engine_chams {

struct LayerBackup {
    uint8_t fillmode;
    uint32_t matflags;
    uint32_t param;
    uint32_t flags2;
    uint32_t color;
};

struct EntityBackup {
    uint32_t render_queue_id;
};

static std::unordered_map<uintptr_t, LayerBackup> g_backups;
static std::unordered_map<uintptr_t, EntityBackup> g_entity_backups;
static std::unordered_map<uintptr_t, std::vector<uintptr_t>> g_entity_layers;
static std::unordered_set<uintptr_t> g_known_entities;
static std::mutex g_mtx;

inline bool is_valid_ptr(uintptr_t ptr) {
    return ptr >= 0x10000 && ptr < 0x7FFFFFFEFFFFull;
}

static void restore_entity_locked(uintptr_t entity)
{
    auto layers_it = g_entity_layers.find(entity);
    if (layers_it != g_entity_layers.end()) {
        for (uintptr_t layer : layers_it->second) {
            if (!is_valid_ptr(layer)) continue;
            auto backup_it = g_backups.find(layer);
            if (backup_it == g_backups.end())
                continue;

            const auto& b = backup_it->second;
            memory->write<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte, b.fillmode);
            memory->write<uint32_t>(layer + Offsets::MaterialLayer::MatFlags, b.matflags);
            memory->write<uint32_t>(layer + Offsets::MaterialLayer::Param, b.param);
            memory->write<uint32_t>(layer + Offsets::MaterialLayer::Flags2, b.flags2);
            memory->write<uint32_t>(layer + Offsets::MaterialLayer::ColorData, b.color);
            g_backups.erase(backup_it);
        }
        g_entity_layers.erase(layers_it);
    }

    auto entity_backup_it = g_entity_backups.find(entity);
    if (entity_backup_it != g_entity_backups.end()) {
        if (is_valid_ptr(entity)) {
            memory->write<uint32_t>(entity + Offsets::FastClusterEntity::RenderQueueId,
                entity_backup_it->second.render_queue_id);
        }
        g_entity_backups.erase(entity_backup_it);
    }

    g_known_entities.erase(entity);
}

inline bool is_valid_technique_array(uintptr_t entity) {
    if (!is_valid_ptr(entity)) return false;

    uintptr_t mod_base = memory->m_base_address;
    if (!mod_base) return false;

    uintptr_t base_address = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
    if (!base_address || base_address < 0x10000 || base_address > 0x7FFFFFFFFFFFull)
        base_address = mod_base;

    uintptr_t vtable_va1 = base_address + Offsets::FastClusterEntity::VTableRva;
    uintptr_t vtable_va2 = base_address + RVA::FastClusterEntity::VTableRva;
    uintptr_t vtable_va3 = base_address + RVA::Chams::Confirmed::RenderEntityVTable;

    uintptr_t vt = memory->read<uintptr_t>(entity);
    if (vt != vtable_va1 && vt != vtable_va2 && vt != vtable_va3) return false;

    uintptr_t arr = memory->read<uintptr_t>(entity + Offsets::FastClusterEntity::TechniqueArrayPtr);
    if (!is_valid_ptr(arr)) return false;

    uintptr_t begin = memory->read<uintptr_t>(arr + Offsets::TechniqueArray::BeginOffset);
    uintptr_t end = memory->read<uintptr_t>(arr + Offsets::TechniqueArray::EndOffset);
    if (!is_valid_ptr(begin) || !is_valid_ptr(end) || end <= begin) return false;

    size_t bytes = end - begin;
    if (bytes > 64 * 1024) return false;

    size_t count = bytes / Offsets::MaterialLayer::Stride;
    if (count == 0 || count > 256) return false;

    return true;
}

inline void material_set(uintptr_t entity)
{
    if (!settings::visuals::engine_chams || !is_valid_technique_array(entity))
        return;

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_entity_backups.count(entity)) {
            EntityBackup b{};
            b.render_queue_id = memory->read<uint32_t>(entity + Offsets::FastClusterEntity::RenderQueueId);
            g_entity_backups[entity] = b;
        }
    }

    uint32_t queue_id = 10;
    if (settings::visuals::engine_chams_mode == 0)
        queue_id = 10; // Glow
    else if (settings::visuals::engine_chams_mode == 1)
        queue_id = 14; // Invisible
    else if (settings::visuals::engine_chams_mode == 2)
        queue_id = 13; // Always On Top
    else
        queue_id = static_cast<uint32_t>(settings::visuals::engine_chams_queue_id);

    memory->write<uint32_t>(entity + Offsets::FastClusterEntity::RenderQueueId, queue_id);

    uintptr_t arr = memory->read<uintptr_t>(entity + Offsets::FastClusterEntity::TechniqueArrayPtr);
    uintptr_t begin = memory->read<uintptr_t>(arr + Offsets::TechniqueArray::BeginOffset);
    uintptr_t end = memory->read<uintptr_t>(arr + Offsets::TechniqueArray::EndOffset);
    size_t count = (end - begin) / Offsets::MaterialLayer::Stride;

    uint8_t selected_fill_mode = 0;
    uint32_t custom_param = static_cast<uint32_t>(settings::visuals::engine_chams_color_index + 1);
    uint32_t custom_flags2 = 0;

    switch (settings::visuals::engine_chams_style) {
    case 0: // Wireframe Normal
        selected_fill_mode = 0;
        custom_flags2 = 0;
        custom_param = static_cast<uint32_t>(settings::visuals::engine_chams_color_index + 1);
        break;
    case 1: // Wireframe Textured
        selected_fill_mode = 1;
        custom_flags2 = 0;
        custom_param = static_cast<uint32_t>(settings::visuals::engine_chams_color_index + 1);
        break;
    case 2: // Character Wireframe
        selected_fill_mode = 1;
        custom_flags2 = 0;
        custom_param = static_cast<uint32_t>(settings::visuals::engine_chams_color_index + 1);
        break;
    case 3: // Character Mesh
        selected_fill_mode = 0;
        custom_flags2 = 15;
        custom_param = static_cast<uint32_t>(settings::visuals::engine_chams_color_index + 1);
        break;
    default:
        break;
    }

    for (size_t i = 0; i < count; ++i) {
        uintptr_t layer = begin + i * Offsets::MaterialLayer::Stride;
        if (!is_valid_ptr(layer)) continue;

        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (!g_backups.count(layer)) {
                LayerBackup b{};
                b.fillmode = memory->read<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte);
                b.matflags = memory->read<uint32_t>(layer + Offsets::MaterialLayer::MatFlags);
                b.param = memory->read<uint32_t>(layer + Offsets::MaterialLayer::Param);
                b.flags2 = memory->read<uint32_t>(layer + Offsets::MaterialLayer::Flags2);
                b.color = memory->read<uint32_t>(layer + Offsets::MaterialLayer::ColorData);
                g_backups[layer] = b;
                g_entity_layers[entity].push_back(layer);
            }
        }

        memory->write<uint8_t>(layer + Offsets::MaterialLayer::FillModeByte, selected_fill_mode);
        memory->write<uint32_t>(layer + Offsets::MaterialLayer::MatFlags, 0);
        memory->write<uint32_t>(layer + Offsets::MaterialLayer::Param, custom_param);
        memory->write<uint32_t>(layer + Offsets::MaterialLayer::Flags2, custom_flags2);
        memory->write<uint32_t>(layer + Offsets::MaterialLayer::ColorData, 0xFFFFFFFFu);
    }
}

inline void refresh_known_entities()
{
    std::vector<uintptr_t> entities;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        entities.reserve(g_known_entities.size());
        for (uintptr_t e : g_known_entities)
            entities.push_back(e);
    }

    uintptr_t base_address = memory->m_base_address;
    if (!base_address) return;

    for (uintptr_t e : entities) {
        if (!is_valid_technique_array(e)) {
            std::lock_guard<std::mutex> lk(g_mtx);
            restore_entity_locked(e);
            continue;
        }
        material_set(e);
    }
}

inline void restore_all()
{
    std::lock_guard<std::mutex> lk(g_mtx);

    for (auto& [addr, b] : g_backups) {
        if (is_valid_ptr(addr)) {
            memory->write<uint8_t>(addr + Offsets::MaterialLayer::FillModeByte, b.fillmode);
            memory->write<uint32_t>(addr + Offsets::MaterialLayer::MatFlags, b.matflags);
            memory->write<uint32_t>(addr + Offsets::MaterialLayer::Param, b.param);
            memory->write<uint32_t>(addr + Offsets::MaterialLayer::Flags2, b.flags2);
            memory->write<uint32_t>(addr + Offsets::MaterialLayer::ColorData, b.color);
        }
    }
    g_backups.clear();

    for (auto& [addr, b] : g_entity_backups) {
        if (is_valid_ptr(addr)) {
            memory->write<uint32_t>(addr + Offsets::FastClusterEntity::RenderQueueId, b.render_queue_id);
        }
    }
    g_entity_backups.clear();
    g_entity_layers.clear();
}

inline void thread()
{
    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    uintptr_t max_va = (uintptr_t)si.lpMaximumApplicationAddress;
    MEMORY_BASIC_INFORMATION mbi{};
    static std::vector<uint8_t> buf;
    bool was_enabled = false;
    auto last_full_scan = std::chrono::steady_clock::now();

    while (true) {
        HANDLE proc = memory->m_process_handle;
        uintptr_t mod_base = memory->m_base_address;
        uintptr_t base_address = memory->read<uintptr_t>(mod_base + Offsets::BaseAddress);
        if (!base_address || base_address < 0x10000 || base_address > 0x7FFFFFFFFFFFull)
            base_address = mod_base;

        if (!proc || !base_address || !memory->is_attached()) {
            if (was_enabled) {
                restore_all();
                std::lock_guard<std::mutex> lk(g_mtx);
                g_known_entities.clear();
                was_enabled = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        if (settings::visuals::engine_chams) {
            if (!was_enabled) {
                std::lock_guard<std::mutex> lk(g_mtx);
                g_backups.clear();
                g_entity_backups.clear();
                g_known_entities.clear();
                g_entity_layers.clear();
                was_enabled = true;
                last_full_scan = std::chrono::steady_clock::now() - std::chrono::seconds(10);
            }

            auto now = std::chrono::steady_clock::now();
            bool do_full_scan = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_full_scan).count() >= 1000;

            if (do_full_scan) {
                last_full_scan = now;
                uintptr_t vtable_va1 = base_address + Offsets::FastClusterEntity::VTableRva;
                uintptr_t vtable_va2 = base_address + RVA::FastClusterEntity::VTableRva;
                uintptr_t vtable_va3 = base_address + RVA::Chams::Confirmed::RenderEntityVTable;

                uintptr_t addr = (uintptr_t)si.lpMinimumApplicationAddress;
                std::vector<uintptr_t> found_entities;

                while (addr < max_va) {
                    if (VirtualQueryEx(proc, (LPCVOID)addr, &mbi, sizeof(mbi)) == 0)
                        break;

                    const bool readable_prot =
                        mbi.Protect == PAGE_READWRITE ||
                        mbi.Protect == PAGE_EXECUTE_READWRITE ||
                        mbi.Protect == PAGE_WRITECOPY ||
                        mbi.Protect == PAGE_EXECUTE_WRITECOPY;

                    const bool scannable =
                        mbi.State == MEM_COMMIT &&
                        mbi.Type != MEM_IMAGE &&
                        readable_prot;

                    if (scannable && mbi.RegionSize <= 64 * 1024 * 1024) {
                        uintptr_t region_start = (uintptr_t)mbi.BaseAddress;
                        size_t region_size = mbi.RegionSize;
                        if (buf.size() < region_size)
                            buf.resize(region_size);

                        SIZE_T got = 0;
                        if (ReadProcessMemory(proc, (LPCVOID)region_start, buf.data(), region_size, &got) && got >= 16) {
                            for (size_t i = 0; i + 16 <= got; i += 8) {
                                uintptr_t vt = *(const uintptr_t*)(buf.data() + i);
                                if (vt != vtable_va1 && vt != vtable_va2 && vt != vtable_va3)
                                    continue;

                                uintptr_t entity = region_start + i;
                                found_entities.push_back(entity);
                            }
                        }
                    }

                    uintptr_t next = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
                    if (next <= addr)
                        break;
                    addr = next;
                }

                if (!found_entities.empty()) {
                    std::lock_guard<std::mutex> lk(g_mtx);
                    for (uintptr_t entity : found_entities) {
                        if (is_valid_technique_array(entity)) {
                            g_known_entities.insert(entity);
                        }
                    }
                }
            }

            refresh_known_entities();
        } else {
            if (was_enabled) {
                restore_all();
                std::lock_guard<std::mutex> lk(g_mtx);
                g_known_entities.clear();
                was_enabled = false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} 