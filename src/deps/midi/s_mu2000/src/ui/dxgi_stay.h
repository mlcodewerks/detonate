// license:BSD-3-Clause
//
// **DXGI をホストのプロセスに残す・窓を見張らせない**（プラグイン用。issue #131）。
//
// DXGI はスワップチェーンを作った窓（とその親）のメッセージを見張る（Alt+Enter の全画面切り替えなど）。
// プラグインが外れると、ほかに使う人のいないホスト（FL Studio）では dxgi.dll も一緒に外れ、見張りの受け口だけが
// ホストの窓に残って、次のメッセージで消えたコードへ飛ぶ（イベントログ: dxgi.dll_unloaded、例外 0xc000041d）。
//   ・見張りをやめさせる（MakeWindowAssociation の NO_WINDOW_CHANGES）
//   ・dxgi.dll と d3d11.dll を外れないように留める（プロセスが終わるまで）
// gui（自分のプロセス）では害が無いので、同じ道を通す。スワップチェーンを作った直後に呼ぶ

#ifndef S_MU2000_UI_DXGI_STAY_H
#define S_MU2000_UI_DXGI_STAY_H

#pragma once

#include <d3d11.h>
#include <windows.h>

namespace ui {

inline void dxgi_stay(IDXGISwapChain *swap, HWND hwnd)
{
	for (const wchar_t *name : { L"dxgi.dll", L"d3d11.dll" }) {
		HMODULE pinned = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, name, &pinned);
	}
	if (!swap || !hwnd)
		return;
	IDXGIFactory *factory = nullptr;
	if (SUCCEEDED(swap->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory))) && factory) {
		factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
		factory->Release();
	}
}

} // namespace ui

#endif // S_MU2000_UI_DXGI_STAY_H
