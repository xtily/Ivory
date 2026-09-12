#pragma once

#include <D3D11.h>
class Menu
{
public:
	static void DrawNavigationBar();
	static bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	static void Shutdown();
	static void ApplyTheme(int index);
	inline static struct ImFont* m_pIconFont = nullptr;
	inline static bool m_bInitialized = false;

	inline static bool m_bShowMenu = true;
	inline static bool m_bShowPlayerList = false;
	inline static bool m_bShowESPPreview = true;
	inline static bool m_bShowIndicator = false;
	inline static bool m_bShowKeybinds = false;
	inline static bool m_bShowExplorer = false;
	inline static bool m_bShowSettings = false;
	inline static bool m_bShowConfig = false;
	inline static bool m_bShowWatermark = true;
	inline static bool m_bShowRivalsSkinChanger = false;

public:
	static void Render();
	static void DrawAll(bool menu_open);
	static bool HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

public:
	static void InvalidateDeviceObjects();
	static void CreateDeviceObjects();

public:
	static void	DrawWatermark();
	static void	DrawKeybinds();
	static void	DrawMenu();
	static void	DrawESPPreview();
	static void	DrawIndicator();
	static void	DrawSettings();
	static void	DrawConfig();
};
