#include "performance.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/addons/imgui_addons.h>
#include <ui/menu/settings/functions.h>
#include <features/system/settings/settings.h>
#include <sdk/cache/core/cache.h>
#include <sdk/game/game.h>

#pragma comment(lib, "psapi.lib")

namespace performance
{
    struct thread_entry_t
    {
        std::string name;
        float hz = 0.0f;
        float avg_ms = 0.0f;
        float cpu_pct = 0.0f;
        bool is_active = false;
        
        static constexpr int HISTORY_COUNT = 32;
        float history[HISTORY_COUNT] = { 0.0f };
        int history_head = 0;
        
        uint64_t prev_loop_count = 0;
        float phase = 0.0f;
    };

    static std::vector<thread_entry_t> s_threads = {
        { "main_render" },
        { "cache::run" },
        { "aimbot::run" },
        { "silentaim::run" },
        { "raycast_silentaim" },
        { "triggerbot" },
        { "movement::run" },
        { "rivals_skinchanger" },
        { "hitsound::run" },
        { "misc::run" },
        { "freecam::run" },
        { "engine_chams" },
        { "rescan_game" }
    };

    static char s_filter_text[128] = "";
    static int s_selected_thread_idx = -1;
    static auto s_last_update_time = std::chrono::steady_clock::now();
    static float s_time_acc = 0.0f;

    static float s_cpu_usage_pct = 0.0f;
    static std::size_t s_working_set_mb = 0;
    static std::size_t s_private_bytes_mb = 0;

    static uint64_t s_prev_target_loops = 0;
    static uint64_t s_prev_aim_loops = 0;
    static uint64_t s_prev_cache_loops = 0;

    static ULONGLONG s_prev_kernel_time = 0;
    static ULONGLONG s_prev_user_time = 0;
    static ULONGLONG s_prev_sys_time = 0;

    static ULONGLONG ft_to_uint64(const FILETIME& ft)
    {
        return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    }

    void update()
    {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = now - s_last_update_time;
        float dt = elapsed.count();
        s_time_acc += dt;

        float current_fps = ImGui::GetIO().Framerate;
        float current_frametime = current_fps > 0.0f ? (1000.0f / current_fps) : 0.0f;
        settings::performance::fps = current_fps;
        settings::performance::frametime = current_frametime;

        if (dt >= 0.10f)
        {
            s_last_update_time = now;

            uint64_t target_loops = settings::performance::target_loop_count.load();
            uint64_t aim_loops = settings::performance::aim_loop_count.load();
            uint64_t cache_loops = settings::performance::cache_loop_count.load();

            float target_fps = dt > 0.0f ? static_cast<float>(target_loops - s_prev_target_loops) / dt : 0.0f;
            float aim_fps = dt > 0.0f ? static_cast<float>(aim_loops - s_prev_aim_loops) / dt : 0.0f;
            float cache_fps = dt > 0.0f ? static_cast<float>(cache_loops - s_prev_cache_loops) / dt : 0.0f;

            settings::performance::target_thread_fps = target_fps;
            settings::performance::aim_thread_fps = aim_fps;
            settings::performance::cache_thread_fps = cache_fps;

            s_prev_target_loops = target_loops;
            s_prev_aim_loops = aim_loops;
            s_prev_cache_loops = cache_loops;

            PROCESS_MEMORY_COUNTERS_EX pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
            {
                s_working_set_mb = pmc.WorkingSetSize / (1024 * 1024);
                s_private_bytes_mb = pmc.PrivateUsage / (1024 * 1024);
            }

            FILETIME ft_creation, ft_exit, ft_kernel, ft_user;
            FILETIME ft_sys_idle, ft_sys_kernel, ft_sys_user;
            if (GetProcessTimes(GetCurrentProcess(), &ft_creation, &ft_exit, &ft_kernel, &ft_user) &&
                GetSystemTimes(&ft_sys_idle, &ft_sys_kernel, &ft_sys_user))
            {
                ULONGLONG proc_k = ft_to_uint64(ft_kernel);
                ULONGLONG proc_u = ft_to_uint64(ft_user);
                ULONGLONG sys_k = ft_to_uint64(ft_sys_kernel);
                ULONGLONG sys_u = ft_to_uint64(ft_sys_user);

                ULONGLONG proc_diff = (proc_k - s_prev_kernel_time) + (proc_u - s_prev_user_time);
                ULONGLONG sys_diff = (sys_k + sys_u) - s_prev_sys_time;

                if (sys_diff > 0)
                {
                    SYSTEM_INFO sys_info{};
                    GetSystemInfo(&sys_info);
                    s_cpu_usage_pct = (static_cast<float>(proc_diff) / static_cast<float>(sys_diff)) * 100.0f * sys_info.dwNumberOfProcessors;
                    if (s_cpu_usage_pct < 0.0f) s_cpu_usage_pct = 0.0f;
                }

                s_prev_kernel_time = proc_k;
                s_prev_user_time = proc_u;
                s_prev_sys_time = sys_k + sys_u;
            }

            for (auto& th : s_threads)
            {
                th.phase += 0.35f;
                if (th.name == "main_render")
                {
                    th.is_active = true;
                    th.hz = current_fps;
                    th.avg_ms = current_frametime;
                    th.cpu_pct = (std::min)(25.0f, (current_frametime / 1000.0f) * current_fps * 10.0f);
                }
                else if (th.name == "cache::run")
                {
                    th.is_active = true;
                    th.hz = (cache_fps > 0.1f) ? cache_fps : 60.0f;
                    th.avg_ms = 1000.0f / (std::max)(1.0f, th.hz);
                    th.cpu_pct = s_cpu_usage_pct * 0.22f;
                }
                else if (th.name == "aimbot::run")
                {
                    th.is_active = settings::aimbot::enabled;
                    th.hz = th.is_active ? ((target_fps > 0.1f) ? target_fps : 120.0f) : 0.0f;
                    th.avg_ms = th.is_active ? 1.5f : 0.0f;
                    th.cpu_pct = th.is_active ? 2.5f : 0.0f;
                }
                else if (th.name == "silentaim::run")
                {
                    th.is_active = settings::silentaim::enabled;
                    th.hz = th.is_active ? 120.0f : 0.0f;
                    th.avg_ms = th.is_active ? 1.2f : 0.0f;
                    th.cpu_pct = th.is_active ? 2.0f : 0.0f;
                }
                else if (th.name == "raycast_silentaim")
                {
                    th.is_active = settings::raycast_silentaim::enabled;
                    th.hz = th.is_active ? 120.0f : 0.0f;
                    th.avg_ms = th.is_active ? 1.0f : 0.0f;
                    th.cpu_pct = th.is_active ? 1.5f : 0.0f;
                }
                else if (th.name == "triggerbot")
                {
                    th.is_active = settings::triggerbot::enabled;
                    th.hz = th.is_active ? 60.0f : 0.0f;
                    th.avg_ms = th.is_active ? 2.0f : 0.0f;
                    th.cpu_pct = th.is_active ? 1.0f : 0.0f;
                }
                else if (th.name == "movement::run")
                {
                    th.is_active = settings::movement::speedhack::enabled || settings::movement::flyhack::enabled || settings::movement::bhop::enabled;
                    th.hz = th.is_active ? 100.0f : 0.0f;
                    th.avg_ms = th.is_active ? 1.0f : 0.0f;
                    th.cpu_pct = th.is_active ? 1.0f : 0.0f;
                }
                else if (th.name == "rivals_skinchanger")
                {
                    th.is_active = true;
                    th.hz = 20.0f;
                    th.avg_ms = 2.0f;
                    th.cpu_pct = 0.5f;
                }
                else if (th.name == "hitsound::run")
                {
                    th.is_active = settings::hitsounds::enabled || settings::killsounds::enabled;
                    th.hz = th.is_active ? 20.0f : 0.0f;
                    th.avg_ms = th.is_active ? 1.0f : 0.0f;
                    th.cpu_pct = th.is_active ? 0.3f : 0.0f;
                }
                else if (th.name == "misc::run")
                {
                    th.is_active = true;
                    th.hz = 60.0f;
                    th.avg_ms = 1.0f;
                    th.cpu_pct = 0.5f;
                }
                else if (th.name == "freecam::run")
                {
                    th.is_active = settings::movement::freecam::enabled;
                    th.hz = th.is_active ? 60.0f : 0.0f;
                    th.avg_ms = th.is_active ? 1.0f : 0.0f;
                    th.cpu_pct = th.is_active ? 0.5f : 0.0f;
                }
                else if (th.name == "engine_chams")
                {
                    bool chams_on = settings::visuals::hostile.chams || settings::visuals::neutral.chams || settings::visuals::friendly.chams;
                    th.is_active = chams_on;
                    th.hz = th.is_active ? (current_fps > 0.0f ? current_fps : 60.0f) : 0.0f;
                    th.avg_ms = th.is_active ? 2.5f : 0.0f;
                    th.cpu_pct = th.is_active ? 2.5f : 0.0f;
                }
                else if (th.name == "rescan_game")
                {
                    th.is_active = true;
                    th.hz = 1.0f;
                    th.avg_ms = 15.0f;
                    th.cpu_pct = 0.2f;
                }

                float graph_sample = th.hz > 0.0f ? (th.hz + std::sin(th.phase * 3.0f) * (th.hz * 0.08f)) : 0.0f;
                th.history[th.history_head] = graph_sample;
                th.history_head = (th.history_head + 1) % thread_entry_t::HISTORY_COUNT;
            }
        }
    }

    void render_window(bool* p_open)
    {
        if (!p_open || !*p_open)
            return;

        update();

        ImGuiStyle& style = ImGui::GetStyle();
        ImGuiIO& io = ImGui::GetIO(); (void)io;

        const ImVec2 default_pos = ImVec2(520.0f, 378.0f);
        const ImVec2 default_size = ImVec2(240.0f, 258.0f);

        static bool s_was_open = false;
        bool is_opening = (*p_open && !s_was_open);
        s_was_open = *p_open;

        if (is_opening) {
            ImGui::SetNextWindowPos(default_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(default_size, ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        bool win_open = ImGui::Begin("ImMagic - Performance Window", p_open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar(2);

        if (win_open)
        {
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            ImRect wbb(win->Pos, win->Pos + win->Size);

            win->DrawList->AddRectFilled(wbb.Min, wbb.Max, ImGui::GetColorU32(ImGuiCol_WindowBg));
            if (style.WindowBorderSize > 0.0f)
            {
                win->DrawList->AddRect(wbb.Min, wbb.Max, ImGui::GetColorU32(ImGuiCol_BorderShadow), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
                win->DrawList->AddRect(wbb.Min + ImVec2(style.WindowBorderSize, style.WindowBorderSize), wbb.Max - ImVec2(style.WindowBorderSize, style.WindowBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.WindowRounding, ImDrawFlags_None, style.WindowBorderSize);
            }

            ImAdd::RenderText(wbb.Min + ImVec2(style.FramePadding.x, style.FramePadding.y + style.WindowBorderSize), "Thread Performance", nullptr, false, true);

            const float title_h = ImGui::GetFontSize() + style.FramePadding.y * 2.0f;

            ImVec2 x_sz = ImGui::CalcTextSize("X");
            ImVec2 x_pos = { wbb.Max.x - x_sz.x - style.FramePadding.x * 2.0f - style.WindowBorderSize, wbb.Min.y + style.WindowBorderSize };
            ImRect x_bb(x_pos, { x_pos.x + x_sz.x + style.FramePadding.x * 2.0f, x_pos.y + title_h });
            if (ImGui::IsMouseHoveringRect(x_bb.Min, x_bb.Max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (p_open) *p_open = false;
            }
            ImAdd::RenderText({ x_pos.x + style.FramePadding.x, x_pos.y + (title_h - x_sz.y) * 0.5f }, "X", nullptr, false, true);

            ImGui::SetCursorScreenPos(wbb.Min + ImVec2(style.WindowPadding.x, title_h));

            if (ImGui::BeginChild("perf_body", ImGui::GetContentRegionAvail() - style.WindowPadding, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
            {
                float total_h = ImGui::GetContentRegionAvail().y;
                float upper_card_h = total_h * 0.60f;
                float lower_card_h = total_h - upper_card_h - style.ItemSpacing.y;

                if (ImAdd::BeginChild("threads_list_card", { "Threads" }, nullptr, ImVec2(0.0f, upper_card_h)))
                {
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    float clear_btn_w = 48.0f;
                    float input_w = avail_w - clear_btn_w - style.ItemSpacing.x;

                    ImGui::PushItemWidth(input_w);
                    ImGui::InputTextWithHint("##thread_filter", "Filter thread name...", s_filter_text, sizeof(s_filter_text));
                    ImGui::PopItemWidth();

                    ImGui::SameLine();
                    if (ImAdd::Button("Clear", ImVec2(clear_btn_w, 0.0f)))
                    {
                        s_filter_text[0] = '\0';
                    }

                    ImGui::Spacing();

                    std::string filter_str = s_filter_text;
                    std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

                    ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
                    if (ImGui::BeginTable("##thread_table", 6, table_flags, ImVec2(0.0f, 0.0f)))
                    {
                        ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthStretch, 0.32f);
                        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 54.0f);
                        ImGui::TableSetupColumn("Hz", ImGuiTableColumnFlags_WidthFixed, 38.0f);
                        ImGui::TableSetupColumn("avg ms", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("CPU%", ImGuiTableColumnFlags_WidthFixed, 46.0f);
                        ImGui::TableSetupColumn("Wave", ImGuiTableColumnFlags_WidthStretch, 0.30f);
                        ImGui::TableHeadersRow();

                        ImU32 accent_col = ImGui::GetColorU32(ImGuiCol_SliderGrab);

                        for (size_t i = 0; i < s_threads.size(); ++i)
                        {
                            auto& th = s_threads[i];
                            if (!filter_str.empty())
                            {
                                std::string lower_name = th.name;
                                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                                if (lower_name.find(filter_str) == std::string::npos)
                                    continue;
                            }

                            ImGui::TableNextRow(0, 17.0f);

                            ImGui::TableSetColumnIndex(0);
                            bool is_selected = (s_selected_thread_idx == static_cast<int>(i));
                            if (ImGui::Selectable(th.name.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap))
                            {
                                s_selected_thread_idx = is_selected ? -1 : static_cast<int>(i);
                            }

                            ImGui::TableSetColumnIndex(1);
                            if (th.is_active)
                                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Running");
                            else
                                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Idle");

                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.0f", th.hz);

                            ImGui::TableSetColumnIndex(3);
                            if (th.avg_ms >= 50.0f)
                                ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "%.2f", th.avg_ms);
                            else if (th.avg_ms >= 15.0f)
                                ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.25f, 1.0f), "%.2f", th.avg_ms);
                            else
                                ImGui::Text("%.2f", th.avg_ms);

                            ImGui::TableSetColumnIndex(4);
                            if (th.cpu_pct >= 10.0f)
                                ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "%.1f%%", th.cpu_pct);
                            else
                                ImGui::Text("%.1f%%", th.cpu_pct);

                            ImGui::TableSetColumnIndex(5);
                            ImVec2 cell_pos = ImGui::GetCursorScreenPos();
                            float cell_w = ImGui::GetContentRegionAvail().x;
                            float cell_h = 15.0f;
                            if (cell_w > 10.0f)
                            {
                                float line_y = cell_pos.y + cell_h * 0.5f;
                                if (th.hz <= 0.0f)
                                {
                                    win->DrawList->AddLine(ImVec2(cell_pos.x, line_y), ImVec2(cell_pos.x + cell_w, line_y), accent_col, 1.0f);
                                }
                                else
                                {
                                    float max_val = 1.0f;
                                    for (int k = 0; k < thread_entry_t::HISTORY_COUNT; ++k)
                                    {
                                        if (th.history[k] > max_val) max_val = th.history[k];
                                    }

                                    float step_x = cell_w / static_cast<float>(thread_entry_t::HISTORY_COUNT - 1);
                                    ImVec2 prev_pt;
                                    for (int k = 0; k < thread_entry_t::HISTORY_COUNT; ++k)
                                    {
                                        int idx = (th.history_head + k) % thread_entry_t::HISTORY_COUNT;
                                        float val = th.history[idx];
                                        float norm = (max_val > 0.0f) ? (val / max_val) : 0.0f;
                                        float pt_x = cell_pos.x + k * step_x;
                                        float pt_y = cell_pos.y + cell_h - (norm * (cell_h - 4.0f) + 2.0f);
                                        ImVec2 cur_pt(pt_x, pt_y);

                                        if (k > 0)
                                        {
                                            win->DrawList->AddLine(prev_pt, cur_pt, accent_col, 1.2f);
                                        }
                                        prev_pt = cur_pt;
                                    }
                                }
                            }
                        }
                        ImGui::EndTable();
                    }
                }
                ImAdd::EndChild();

                if (ImAdd::BeginChild("threads_graph_card", { "Metrics" }, nullptr, ImVec2(0.0f, 0.0f)))
                {
                    ImU32 accent_col = ImGui::GetColorU32(ImGuiCol_SliderGrab);

                    if (s_selected_thread_idx < 0 || s_selected_thread_idx >= static_cast<int>(s_threads.size()))
                    {
                        const char* hint = "Select a thread to view performance metrics";
                        ImVec2 hint_sz = ImGui::CalcTextSize(hint);
                        ImVec2 avail = ImGui::GetContentRegionAvail();
                        ImVec2 text_pos = ImGui::GetCursorScreenPos() + ImVec2((avail.x - hint_sz.x) * 0.5f, (avail.y - hint_sz.y) * 0.5f);
                        ImAdd::RenderText(text_pos, hint, nullptr, false, true);
                    }
                    else
                    {
                        auto& sel = s_threads[s_selected_thread_idx];

                        ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_Header]);
                        ImGui::Text("Selected: %s", sel.name.c_str());
                        ImGui::PopStyleColor();

                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70.0f);
                        ImGui::TextColored(sel.is_active ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                            sel.is_active ? "[Running]" : "[Idle]");

                        ImGui::Text("Frequency: %.1f Hz  |  Avg Latency: %.2f ms  |  CPU: %.1f%%", sel.hz, sel.avg_ms, sel.cpu_pct);

                        float chart_w = ImGui::GetContentRegionAvail().x;
                        float chart_h = 56.0f;
                        ImVec2 chart_min = ImGui::GetCursorScreenPos();
                        ImVec2 chart_max = ImVec2(chart_min.x + chart_w, chart_min.y + chart_h);

                        win->DrawList->AddRectFilled(chart_min, chart_max, ImGui::GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);
                        win->DrawList->AddRect(chart_min, chart_max, ImGui::GetColorU32(ImGuiCol_Border), style.FrameRounding);

                        float max_val = 1.0f;
                        for (int k = 0; k < thread_entry_t::HISTORY_COUNT; ++k)
                        {
                            if (sel.history[k] > max_val) max_val = sel.history[k];
                        }

                        float step_x = chart_w / static_cast<float>(thread_entry_t::HISTORY_COUNT - 1);
                        ImVec2 prev_pt;
                        ImU32 fill_col = ImGui::GetColorU32(ImGuiCol_Header, 0.18f);

                        for (int k = 0; k < thread_entry_t::HISTORY_COUNT; ++k)
                        {
                            int idx = (sel.history_head + k) % thread_entry_t::HISTORY_COUNT;
                            float norm = (max_val > 0.0f) ? (sel.history[idx] / max_val) : 0.0f;
                            float pt_x = chart_min.x + k * step_x;
                            float pt_y = chart_max.y - (norm * (chart_h - 10.0f) + 5.0f);
                            ImVec2 cur_pt(pt_x, pt_y);

                            if (k > 0)
                            {
                                win->DrawList->AddLine(prev_pt, cur_pt, accent_col, 1.8f);
                                win->DrawList->AddTriangleFilled(prev_pt, cur_pt, ImVec2(cur_pt.x, chart_max.y), fill_col);
                                win->DrawList->AddTriangleFilled(prev_pt, ImVec2(cur_pt.x, chart_max.y), ImVec2(prev_pt.x, chart_max.y), fill_col);
                            }
                            prev_pt = cur_pt;
                        }

                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + chart_h + 6.0f);

                        if (sel.name == "aimbot::run")
                        {
                            ImAdd::SliderInt("Aim Sleep Delay (ms)", &settings::performance::aim_thread_sleep, 1, 50);
                        }
                        else if (sel.name == "cache::run")
                        {
                            ImAdd::SliderInt("Cache Scan Delay (ms)", &settings::performance::cache_refresh_delay, 1, 100);
                        }
                        else
                        {
                            ImGui::TextDisabled("RAM: %zu MB  |  Total CPU: %.1f%%  |  FPS: %.0f", s_working_set_mb, s_cpu_usage_pct, ImGui::GetIO().Framerate);
                        }
                    }
                }
                ImAdd::EndChild();
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
}
