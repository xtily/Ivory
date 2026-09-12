#include "settings/functions.h"
#include "settings/theme.h"
#include <features/system/settings/settings.h>
#include <features/system/config/config.h>
#include <features/system/notifications/notifications.h>
#include <features/exploits/audio/hitsound/hitsoundsdata.h>
#include <features/lua/vm/LuaVM.h>
#include <features/exploits/environment/explorer/explorer.h>
#include <features/visuals/model/modelviewer.h>
#include <features/visuals/model/default/defaultmodel.h>
#include <features/system/playerlist/playerlist.h>
#include <ui/render/render.h>
#include <sdk/cache/core/cache.h>
#include <ui/editor/editor.h>
#include <sdk/game/game.h>
#include <features/system/waypoints/waypoints.h>
#include <features/visuals/lighting/lighting.h>
#include <features/lua/mcp/mcp_server.h>
#include <features/visuals/manager/manager.h>
#include <features/system/serverbrowser/serverbrowser.h>
#include <features/exploits/locomotion/movement/movement.h>
#include <features/system/keybind/keybind.h>
#include <ui/loader/loader.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <shellapi.h>
#include <Windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

static bool select_sound_file(char* out_path, size_t max_len)
{
	OPENFILENAMEA ofn;
	char file_buf[MAX_PATH] = "";
	if (out_path && strlen(out_path) > 0)
	{
		strncpy_s(file_buf, out_path, sizeof(file_buf) - 1);
	}
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = (::render && ::render->detail) ? ::render->detail->window : NULL;
	ofn.lpstrFile = file_buf;
	ofn.nMaxFile = sizeof(file_buf);
	ofn.lpstrFilter = "Audio Files (*.wav;*.mp3)\0*.wav;*.mp3\0All Files (*.*)\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = "Select Custom Sound File";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn))
	{
		strncpy_s(out_path, max_len, file_buf, max_len - 1);
		return true;
	}
	return false;
}

static const char* server_sort_names[] = { "Lowest Players", "Highest Players", "Lowest Ping", "Highest FPS" };

static editor::lua_editor g_lua_editor;
static bool show_load_confirmation = false;
static bool show_unload_confirmation = false;
static std::string pending_load_config_name = "";
static bool is_overwrite_confirmation = false;

static const char* target_parts[] = { "Head", "Torso", "HumanoidRootPart", "Left Arm", "Right Arm", "Left Leg", "Right Leg", "Closest Part", "Nearest" };
static const char* aim_modes[] = { "Mouse", "Camera" };
static const char* silentaim_methods[] = { "Raycast", "Mouse", "Viewport", "Camera" };
static const char* skybox_presets[] = { "None", "Axis", "Galaxy", "Heaven", "Hell", "Matrix", "Storm", "Sunset", "Night City", "Aurora", "Pink", "Custom" };
static const char* box_types[] = { "2D Corner", "2D Full" };
static const char* tracer_origins[] = { "Bottom", "Center", "Top" };
static const char* chams_types[] = { "Clipper2", "Memory", "Engine" };
static const char* mesh_shader_names[] = {
	"Flat", "Chrome", "Rainbow", "Pearl", "Glossy", "Holographic", "Fade", "Wireframe",
	"Glass", "Copper Ropes", "Liquid Metal", "Soft Glass", "Ice", "Ghost Pulse", "Aurora",
	"Bubble", "Jelly", "Mercury", "Water Caustics", "Deep Ocean", "Quicksilver", "Ripple",
	"Oil Slick", "Metallic", "Void Waves", "Dark Nebula", "Cyber Plasma", "Energy Pulse",
	"Black Hole", "Cosmic Matrix", "Acrylic"
};
static const char* outline_style_names[] = { "Solid Glow", "Soft Fade", "Pulse Glow" };
static const char* hitsound_names[] = { "Skeet", "Neverlose", "Rust", "COD", "Bell", "Bubble", "Bonk" };
static const char* healthbar_gradients[] = { "Vertical", "Horizontal", "Pulse" };
static const char* healthbar_positions[] = { "Left", "Right", "Bottom", "Top" };

void c_gui::render()
{
	explorer::explorer->render_decompiled_windows();
}