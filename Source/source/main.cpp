#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <timeapi.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "winmm.lib")

#include <thread>
#include <chrono>
#include <immintrin.h>


#include <core/globals.h>
#include <sdk/sdk.h>
#include <sdk/game/game.h>
#include <sdk/cache/core/cache.h>
#include <core/logger/logger.h>
#include <ui/render/render.h>
#include <features/system/performance/performance.h>
#include <features/lua/vm/LuaVM.h>
#include <core/scanner/rescan.h>
#include <sdk/wallcheck/wallcheck.h>
#include <core/protection/protection.h>

#include <features/combat/aimbot/aimbot.h>
#include <features/combat/trigger/trigger.h>
#include <features/exploits/locomotion/movement/movement.h>
#include <features/combat/silent/mouse/mouse.h>
#include <features/combat/silent/raycast/raycast.h>
#include <features/combat/hbe/hbe.h>
#include <features/visuals/chams/chams.h>
#include <features/exploits/utility/misc/misc.h>
#include <features/system/config/config.h>
#include <features/visuals/lighting/lighting.h>
#include <features/system/settings/settings.h>
#include <features/exploits/character/avatar/avatar.h>
#include <features/exploits/character/skinchanger/skinchanger.h>
#include <features/exploits/character/dhskinchanger/dhskinchanger.h>
#include <features/exploits/character/rivals_skinchanger/rivals_skinchanger.h>
#include <features/exploits/character/changer/changer.h>
#include <features/exploits/character/materialchanger/materialchanger.h>
#include <features/exploits/utility/npcsystem/npcsystem.h>
#include <features/system/notifications/notifications.h>
#include <features/visuals/model/modelviewer.h>
#include <features/exploits/environment/btools/btools.h>

#include <features/exploits/locomotion/unified/unifiedexploits.h>
#include <features/exploits/audio/hitsound/hitsounds.h>
#include <features/exploits/environment/skybox/skybox.h>
#include <features/exploits/environment/explorer/explorer.h>
#include <features/exploits/utility/freecam/freecam.h>
#include <features/lua/mcp/mcp_server.h>
#include <core/utility/creator/creator.h>
#include <ui/menu/settings/functions.h>
#include <ui/loader/loader.h>

namespace {
	bool should_render_ui() {
		if (var->gui.menu_opened || loader::open) {
			return true;
		}
		HWND hwnd = GetForegroundWindow();
		HWND roblox_window = game::get_roblox_window();
		HWND overlay_window = (render && render->detail) ? render->detail->window : nullptr;

		if (roblox_window != nullptr && IsWindow(roblox_window)) {
			return (hwnd == roblox_window || hwnd == overlay_window);
		}
		return true;
	}

void setup_console() {
	AllocConsole();
	FILE* pCout;
	FILE* pCin;
	FILE* pCerr;
	freopen_s(&pCout, "CONOUT$", "w", stdout);
	freopen_s(&pCin, "CONIN$", "r", stdin);
	freopen_s(&pCerr, "CONOUT$", "w", stderr);

	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut != INVALID_HANDLE_VALUE) {
		DWORD dwMode = 0;
		GetConsoleMode(hOut, &dwMode);
		dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
		SetConsoleMode(hOut, dwMode);
	}

	HWND console_window = GetConsoleWindow();
	if (console_window) {
		ShowWindow(console_window, SW_SHOW);
	}
}

static void ResetGlobals()
{
	game::datamodel = { 0 };
	game::visualengine = { 0 };
}

static void WaitForRoblox()
{
	if (memory->is_attached() && memory->is_alive())
		return;

	if (memory->is_attached())
		memory->detach();

	Cheat::Console::Log(Cheat::Console::Color::Yellow, "waiting for roblox");
	while (!memory->attach_to_process(BINARY_NAME))
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

static void OnRobloxAttached(bool reattached)
{
	memory->find_module_address(BINARY_NAME);
	Cheat::Console::Clear();
	if (reattached)
		Cheat::Console::DumpLastCrash();
}

LONG WINAPI ApplicationCrashHandler(EXCEPTION_POINTERS* exception_info)
{
	return EXCEPTION_EXECUTE_HANDLER;
}
}

namespace {
	template <typename Fn>
	void guarded_thread(const char* name, Fn&& fn)
	{
		std::thread([name, fn = std::forward<Fn>(fn)]() {
			try { fn(); }
			catch (const std::exception& e) { logger->log<ERR>("thread '{}' died: {}", name, e.what()); }
			catch (...) { logger->log<ERR>("thread '{}' died: unknown exception", name); }
		}).detach();
	}
}

std::int32_t main()
{
	using namespace std::chrono_literals;

	paw_guard::init();

	SetProcessDPIAware();
	setup_console();
	SetUnhandledExceptionFilter(ApplicationCrashHandler);
	timeBeginPeriod(1);

	SetConsoleCtrlHandler([](DWORD dwCtrlType) -> BOOL {
		return FALSE;
	}, TRUE);

	std::string console_title = "Ivory";
	SetConsoleTitleA(console_title.c_str());

	Cheat::Console::Log(Cheat::Console::Color::Gray, "configs");
	orok_config.SetupValues();
	config::ensure_config_directory();
	settings::misc::hide_console = false;
	LuaVM::Initialize();
	assetmesh::initialize();
	avatarmesh::initialize();
	memorymesh::start();

	{ 
		if (!render->create_window())
		{
			Cheat::Console::Log(Cheat::Console::Color::Red, "failed to window handle");
			std::this_thread::sleep_for(10s);
			std::exit(0);
		}

		if (!render->create_device())
		{
			std::this_thread::sleep_for(10s);
			std::exit(0);
		}

		if (!render->create_imgui())
		{
			std::this_thread::sleep_for(10s);
			std::exit(0);
		}

		config::load_autoload();
		config::auto_load_theme();

		if (render && render->detail && render->detail->device && render->detail->device_context)
		{
			ModelViewer::g_model_viewer.Init(render->detail->device, render->detail->device_context);
			ModelViewer::g_player_model_viewer.Init(render->detail->device, render->detail->device_context);
		}
	}

	creator::install_console_handler();
	auto start_cheat = []() {
		WaitForRoblox();
		OnRobloxAttached(false);

		game::datamodel = { rbx::c_datamodel::get() };
		game::visualengine = { rbx::c_visualengine::get() };

		Cheat::Console::DumpWorld();

		wallcheck->cache_workspace();

		guarded_thread("rescan_game", rescan::rescan_game);
		guarded_thread("rescan_process", rescan::rescan_process);

		guarded_thread("cache::run", cache::run);
		guarded_thread("movement::run", movement::run);

		guarded_thread("combat", []()
		{
			guarded_thread("aimbot::run", aimbot::run);
			guarded_thread("triggerbot::run", triggerbot::run);
			guarded_thread("raycast_silentaim::run", raycast_silentaim::run);
			guarded_thread("silentaim::run", silentaim::run);
		});

		guarded_thread("features", []()
		{
			guarded_thread("misc::run", misc::run);
			guarded_thread("hitsound::run", hitsound::run);
			guarded_thread("skinchanger::run", exploits::skinchanger::run);
			guarded_thread("dh_skinchanger::run", exploits::dh_skinchanger::run);
			guarded_thread("rivals_skinchanger::run", exploits::rivals_skinchanger::run);
			guarded_thread("animationchanger::run", exploits::animationchanger::run);
			guarded_thread("run_unified", exploits::run_unified);
			guarded_thread("btools::run", btools::run);
			guarded_thread("materialchanger::run", exploits::materialchanger::run);
			guarded_thread("freecam::run", freecam::run);
			guarded_thread("mcp::run", mcp::run);
			guarded_thread("engine_chams::thread", hacks::engine_chams::thread);
		});

		lighting::run_all();

		config::load_autoload();
		config::auto_load_theme();

		loader::is_loaded = true;
	};

	loader::on_load_callback = start_cheat;
	std::thread(start_cheat).detach();

	render->running = true;
	var->gui.menu_opened = false;
	var->gui.menu_alpha = 0.0f;

	render->menu_open = false;
	render->main_window_open = false;

	while (true)
	{
		auto frame_start = std::chrono::high_resolution_clock::now();

		if (loader::is_loaded && !memory->is_alive())
		{
			DWORD code = 0;
			if (!memory->get_exit_code(&code))
				code = 0xFFFFFFFF;

			ResetGlobals();
			creator::notify_datamodel_lost();
			Cheat::Console::Clear();
			Cheat::Console::DumpCrash(code);

			memory->detach();
			WaitForRoblox();
			OnRobloxAttached(true);
			game::datamodel = { rbx::c_datamodel::get() };
			game::visualengine = { rbx::c_visualengine::get() };
			wallcheck->cache_workspace();
		}

		try
		{
		render->start_render();

		HWND foreground_window = GetForegroundWindow();
		HWND roblox_window = game::get_roblox_window();
		HWND overlay_window = (render && render->detail) ? render->detail->window : nullptr;
		static HWND s_last_foreground = nullptr;

		static LONG s_current_exstyle = 0;
		static bool s_last_interactive = false;

		if (foreground_window != s_last_foreground)
		{
			s_last_foreground = foreground_window;
		}

		if (!should_render_ui())
		{
			if (overlay_window && IsWindow(overlay_window)) {
				SetWindowLong(overlay_window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_LAYERED);
				s_current_exstyle = WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_LAYERED;
			}
			render->end_render();
			std::this_thread::sleep_for(20ms);
			continue;
		}

		render->render_visuals();


		static auto last_frame_time = std::chrono::high_resolution_clock::now();
		auto cur_frame_time = std::chrono::high_resolution_clock::now();
		float dt = std::chrono::duration<float>(cur_frame_time - last_frame_time).count();
		last_frame_time = cur_frame_time;

		static bool s_toggle_key_was_down = false;
		bool toggle_key_down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
		if (toggle_key_down && !s_toggle_key_was_down)
		{
			var->gui.menu_opened = !var->gui.menu_opened;
			if (var->gui.menu_opened)
			{
				var->gui.current_section[0] = true;
			}
		}
		s_toggle_key_was_down = toggle_key_down;

		bool cursor_over_loader = false;
		if (loader::open && !var->gui.menu_opened)
		{
			POINT cp;
			GetCursorPos(&cp);
			ImVec2 lpos = loader::pos;
			ImVec2 lsz = loader::get_current_window_size();
			if (cp.x >= (lpos.x - 5.f) && cp.x <= (lpos.x + lsz.x + 5.f) &&
				cp.y >= (lpos.y - 5.f) && cp.y <= (lpos.y + lsz.y + 5.f))
			{
				cursor_over_loader = true;
			}
		}

		const bool interactive_ui = var->gui.menu_opened || cursor_over_loader || loader::is_dragging;

		static bool s_last_menu_state = false;
		if (interactive_ui != s_last_menu_state)
		{
			s_last_menu_state = interactive_ui;
			if (var->gui.menu_opened && overlay_window)
			{
				SetForegroundWindow(overlay_window);
			}
			else if (!interactive_ui && roblox_window && IsWindow(roblox_window))
			{
				SetForegroundWindow(roblox_window);
			}
		}

		if (overlay_window && IsWindow(overlay_window))
		{
			LONG target_style = interactive_ui ? (WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED) : (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_LAYERED);
			if (s_current_exstyle != target_style)
			{
				SetWindowLong(overlay_window, GWL_EXSTYLE, target_style);
				SetWindowPos(overlay_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
				s_current_exstyle = target_style;
			}

			render->render_menu();

			if (settings::misc::npc_system_window && (var->gui.menu_opened || settings::misc::npc_system_pinned))
			{
				ImVec2 main_pos = ImVec2(100.0f, 100.0f);
				ImVec2 main_size = ImVec2(700.0f, 500.0f);
				npcsystem::render_window(&settings::misc::npc_system_window, main_pos, main_size);
			}


			if (settings::misc::explorer_window && (var->gui.menu_opened || settings::misc::explorer_pinned))
			{
				explorer::explorer->render_window(&settings::misc::explorer_window);
			}


		}

		render->end_render();

		if (settings::performance::main_loop_delay > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(settings::performance::main_loop_delay));
		}
		else if (!settings::vsync && settings::misc::unlock_fps && settings::misc::fps_cap > 0)
		{
			auto target_frame_duration = std::chrono::microseconds(1000000 / settings::misc::fps_cap);
			while (std::chrono::high_resolution_clock::now() - frame_start < target_frame_duration)
			{
				std::this_thread::yield();
			}
		}
		}
		catch (...)
		{
			logger->log<ERR>("render error ");
			std::this_thread::sleep_for(50ms);
		}
	}
}