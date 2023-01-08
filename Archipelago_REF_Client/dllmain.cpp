// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
//AP includes
#include <apclient.hpp>
#include <apuuid.hpp>
//REF includes
#include "include/reframework/API.h"
#include <sol.hpp>
//imgui includes
#include "include/imgui/imgui_impl_dx11.h"
#include "include/imgui/imgui_impl_dx12.h"
#include "include/imgui/imgui_impl_win32.h"

#include "include/rendering/d3d11.hpp"
#include "include/rendering/d3d12.hpp"

#include "dllmain.h"

using namespace reframework;

lua_State* g_lua{ nullptr };

HWND g_wnd{};
bool g_initialized{ false };

bool initialize_imgui() {
    if (g_initialized) {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui::GetIO().IniFilename = "example_dll_ui.ini";

    const auto renderer_data = API::get()->param()->renderer_data;

    DXGI_SWAP_CHAIN_DESC swap_desc{};
    auto swapchain = (IDXGISwapChain*)renderer_data->swapchain;
    swapchain->GetDesc(&swap_desc);

    g_wnd = swap_desc.OutputWindow;

    if (!ImGui_ImplWin32_Init(g_wnd)) {
        return false;
    }

    if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D11) {
        if (!g_d3d11.initialize()) {
            return false;
        }
    }
    else if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12) {
        if (!g_d3d12.initialize()) {
            return false;
        }
    }

    g_initialized = true;
    return true;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

