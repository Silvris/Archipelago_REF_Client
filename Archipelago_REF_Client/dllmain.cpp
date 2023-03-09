// dllmain.cpp : Defines the entry point for the DLL application.
//AP includes
#define ASIO_STANDALONE 
#define ASIO_HAS_STD_ADDRESSOF
#define ASIO_HAS_STD_ARRAY
#define ASIO_HAS_CSTDINT
#define ASIO_HAS_STD_SHARED_PTR
#define ASIO_HAS_STD_TYPE_TRAITS
#include <apclient.hpp>
#include <apuuid.hpp>
#include <nlohmann/json.hpp>
//REF includes
#include "include/reframework/API.h"
#include <sol/sol.hpp>
#include <lua.hpp>
//imgui includes
#include <ctype.h>          // toupper
#include <limits.h>         // INT_MIN, INT_MAX
#include <math.h>           // sqrtf, powf, cosf, sinf, floorf, ceilf
#include <stdio.h>          // vsnprintf, sscanf, printf
#include <stdlib.h>         // NULL, malloc, free, atoi
#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>         // intptr_t
#else
#include <stdint.h>         // intptr_t
#endif
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_dx12.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/font_robotomedium.hpp>
#include <rendering/d3d11.hpp>
#include <rendering/d3d12.hpp>
#include <imgui.h>

#include <random>
#include "dllmain.h"


#ifdef __EMSCRIPTEN__
#define DATAPACKAGE_CACHE "/settings/datapackage.json"
#define UUID_FILE "/settings/uuid"
#else
#define DATAPACKAGE_CACHE "datapackage.json" // TODO: place in %appdata%
#define UUID_FILE "uuid" // TODO: place in %appdata%
#endif

using namespace reframework;
using nlohmann::json;

lua_State* g_lua{ nullptr };

std::unordered_map<std::string, sol::load_result> g_loaded_snippets{};

HWND g_wnd{};
bool g_initialized{ false };

APClient* AP = nullptr;

bool isOpen = false;
bool isWithoutRando = true;

// Function Prototypes
bool APSay(std::string msg);
bool ConnectAP(std::string uri = "");

std::default_random_engine randgen;


#pragma region IMGUIFunctions
//-----------------------------------------------------------------------------
// [SECTION] Example App: Debug Console / ShowExampleAppConsole()
//-----------------------------------------------------------------------------

// Demonstrate creating a simple console window, with scrolling, filtering, completion and history.
// For the console example, we are using a more C++ like approach of declaring a class to hold both data and functions.
struct ExampleAppConsole
{
    char                  ConnectBuf[256];
    char                  InputBuf[256];
    char                  UserBuf[256];
    char                  PassBuf[256];
    ImVector<char*>       Items;
    ImVector<const char*> Commands;
    ImVector<char*>       History;
    int                   HistoryPos;    // -1: new line, 0..History.Size-1 browsing history.
    ImGuiTextFilter       Filter;
    bool                  AutoScroll;
    bool                  ScrollToBottom;

    ExampleAppConsole()
    {
        ClearLog();
        memset(InputBuf, 0, sizeof(InputBuf));
        HistoryPos = -1;

        Commands.push_back("/help");
        Commands.push_back("/connect");
        Commands.push_back("/disconnect");
        AutoScroll = true;
        ScrollToBottom = false;
        AddLog("Welcome to Archipelago!");
        ShowHelpInformation();
    }
    ~ExampleAppConsole()
    {
        ClearLog();
        for (int i = 0; i < History.Size; i++)
            free(History[i]);
    }

    // Portable helpers
    static int   Stricmp(const char* s1, const char* s2) { int d; while ((d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; } return d; }
    static int   Strnicmp(const char* s1, const char* s2, int n) { int d = 0; while (n > 0 && (d = toupper(*s2) - toupper(*s1)) == 0 && *s1) { s1++; s2++; n--; } return d; }
    static char* Strdup(const char* s) { IM_ASSERT(s); size_t len = strlen(s) + 1; void* buf = malloc(len); IM_ASSERT(buf); return (char*)memcpy(buf, (const void*)s, len); }
    static void  Strtrim(char* s) { char* str_end = s + strlen(s); while (str_end > s && str_end[-1] == ' ') str_end--; *str_end = 0; }

    void    ShowHelpInformation() {
        AddLog("/help\nShows all possible commands and information about their use.");
        AddLog("/connect [address]\n Attempts to connect to Archipelago at the given address, or the default address if none is given.");
        AddLog("/disconnect\n Disconnects from Archipelago if currently connected.");

    }

    void    ClearLog()
    {
        for (int i = 0; i < Items.Size; i++)
            free(Items[i]);
        Items.clear();
    }

    void    AddLog(const char* fmt, ...) IM_FMTARGS(2)
    {
        // FIXME-OPT
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
        buf[IM_ARRAYSIZE(buf) - 1] = 0;
        va_end(args);
        Items.push_back(Strdup(buf));
    }

    void    Draw(const char* title)
    {
        const auto& api = API::get();
        if (api->reframework()->is_drawing_ui()) {
            bool copy_to_clipboard = false;
            ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin(title))
            {
                ImGui::End();
                return;
            }

            ImGui::InputText(": Server", ConnectBuf, 256);
            ImGui::SameLine();
            // TODO: display items starting from the bottom

            if (ImGui::SmallButton("Connect")) { ConnectAP(std::string(ConnectBuf)); }
            ImGui::InputText(": Username", UserBuf, 256);
            ImGui::InputText(": Password", PassBuf, 256);

            if (ImGui::SmallButton("Copy to Clipboard")) {
                copy_to_clipboard = false;
            }
            //static float t = 0.0f; if (ImGui::GetTime() - t > 0.02f) { t = ImGui::GetTime(); AddLog("Spam %f", t); }

            ImGui::Separator();

            // Options menu
            if (ImGui::BeginPopup("Options"))
            {
                ImGui::Checkbox("Auto-scroll", &AutoScroll);
                ImGui::EndPopup();
            }

            // Options, Filter
            if (ImGui::Button("Options"))
                ImGui::OpenPopup("Options");
            ImGui::Separator();

            // Reserve enough left-over height for 1 separator + 1 input text
            const float footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
            if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar))
            {
                if (ImGui::BeginPopupContextWindow())
                {
                    if (ImGui::Selectable("Clear")) ClearLog();
                    ImGui::EndPopup();
                }

                // Display every line as a separate entry so we can change their color or add custom widgets.
                // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
                // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
                // to only process visible items. The clipper will automatically measure the height of your first item and then
                // "seek" to display only items in the visible area.
                // To use the clipper we can replace your standard loop:
                //      for (int i = 0; i < Items.Size; i++)
                //   With:
                //      ImGuiListClipper clipper;
                //      clipper.Begin(Items.Size);
                //      while (clipper.Step())
                //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++)
                // - That your items are evenly spaced (same height)
                // - That you have cheap random access to your elements (you can access them given their index,
                //   without processing all the ones before)
                // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
                // We would need random-access on the post-filtered list.
                // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
                // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
                // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
                // to improve this example code!
                // If your items are of variable height:
                // - Split them into same height items would be simpler and facilitate random-seeking into your list.
                // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing
                if (copy_to_clipboard)
                    ImGui::LogToClipboard();
                for (int i = 0; i < Items.Size; i++)
                {
                    const char* item = Items[i];
                    if (!Filter.PassFilter(item))
                        continue;

                    // Normally you would store more information in your item than just a string.
                    // (e.g. make Items[] an array of structure, store color/type etc.)
                    ImVec4 color;
                    bool has_color = false;
                    if (strstr(item, "[error]")) { color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); has_color = true; }
                    else if (strncmp(item, "# ", 2) == 0) { color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f); has_color = true; }
                    if (has_color)
                        ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::Text(item);
                    if (has_color)
                        ImGui::PopStyleColor();
                }
                if (copy_to_clipboard)
                    ImGui::LogFinish();

                // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
                // Using a scrollbar or mouse-wheel will take away from the bottom edge.
                if (ScrollToBottom || (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
                    ImGui::SetScrollHereY(1.0f);
                ScrollToBottom = false;

                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::Separator();

            // Command-line
            bool reclaim_focus = false;
            ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll | ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory;
            if (ImGui::InputText("Input", InputBuf, IM_ARRAYSIZE(InputBuf), input_text_flags, &TextEditCallbackStub, (void*)this))
            {
                char* s = InputBuf;
                Strtrim(s);
                if (s[0])
                    ExecCommand(s);
                strcpy(s, "");
                reclaim_focus = true;
            }

            // Auto-focus on window apparition
            ImGui::SetItemDefaultFocus();
            if (reclaim_focus)
                ImGui::SetKeyboardFocusHere(-1); // Auto focus previous widget

            ImGui::End();
        }
    }

    void    ExecCommand(const char* command_line)
    {
        if (command_line[0] == '/') {
            AddLog("# %s\n", command_line);
            // Process command
            if (Stricmp(command_line, "/help") == 0)
            {
                ShowHelpInformation();
            }
            else if (Stricmp(command_line, "/connect") == 0)
            {
                ConnectAP();
            }
            else if (std::string(command_line).find("/connect ") == 0)
            {
                ConnectAP(std::string(command_line).substr(9));
            }
            else if (Stricmp(command_line, "/disconnect")) {

            }
            else if (!AP || !(AP->get_state() > APClient::State::SOCKET_CONNECTED)) {
                AddLog("Not connected to AP, please connect first.");
            }
            else {
                AddLog("Unknown command: '%s'\n", command_line);
            }
        }
        else {
            APSay(std::string(command_line));
        }
        // On command input, we scroll to bottom even if AutoScroll==false
        ScrollToBottom = true;
    }

    // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
    static int TextEditCallbackStub(ImGuiInputTextCallbackData* data)
    {
        ExampleAppConsole* console = (ExampleAppConsole*)data->UserData;
        return console->TextEditCallback(data);
    }

    int     TextEditCallback(ImGuiInputTextCallbackData* data)
    {
        //AddLog("cursor: %d, selection: %d-%d", data->CursorPos, data->SelectionStart, data->SelectionEnd);
        switch (data->EventFlag)
        {
        case ImGuiInputTextFlags_CallbackCompletion:
        {
            // Example of TEXT COMPLETION

            // Locate beginning of current word
            const char* word_end = data->Buf + data->CursorPos;
            const char* word_start = word_end;
            while (word_start > data->Buf)
            {
                const char c = word_start[-1];
                if (c == ' ' || c == '\t' || c == ',' || c == ';')
                    break;
                word_start--;
            }

            // Build a list of candidates
            ImVector<const char*> candidates;
            for (int i = 0; i < Commands.Size; i++)
                if (Strnicmp(Commands[i], word_start, (int)(word_end - word_start)) == 0)
                    candidates.push_back(Commands[i]);

            if (candidates.Size == 0)
            {
                // No match
                AddLog("No match for \"%.*s\"!\n", (int)(word_end - word_start), word_start);
            }
            else if (candidates.Size == 1)
            {
                // Single match. Delete the beginning of the word and replace it entirely so we've got nice casing.
                data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
                data->InsertChars(data->CursorPos, candidates[0]);
                data->InsertChars(data->CursorPos, " ");
            }
            else
            {
                // Multiple matches. Complete as much as we can..
                // So inputing "C"+Tab will complete to "CL" then display "CLEAR" and "CLASSIFY" as matches.
                int match_len = (int)(word_end - word_start);
                for (;;)
                {
                    int c = 0;
                    bool all_candidates_matches = true;
                    for (int i = 0; i < candidates.Size && all_candidates_matches; i++)
                        if (i == 0)
                            c = toupper(candidates[i][match_len]);
                        else if (c == 0 || c != toupper(candidates[i][match_len]))
                            all_candidates_matches = false;
                    if (!all_candidates_matches)
                        break;
                    match_len++;
                }

                if (match_len > 0)
                {
                    data->DeleteChars((int)(word_start - data->Buf), (int)(word_end - word_start));
                    data->InsertChars(data->CursorPos, candidates[0], candidates[0] + match_len);
                }

                // List matches
                AddLog("Possible matches:\n");
                for (int i = 0; i < candidates.Size; i++)
                    AddLog("- %s\n", candidates[i]);
            }

            break;
        }
        case ImGuiInputTextFlags_CallbackHistory:
        {
            // Example of HISTORY
            const int prev_history_pos = HistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow)
            {
                if (HistoryPos == -1)
                    HistoryPos = History.Size - 1;
                else if (HistoryPos > 0)
                    HistoryPos--;
            }
            else if (data->EventKey == ImGuiKey_DownArrow)
            {
                if (HistoryPos != -1)
                    if (++HistoryPos >= History.Size)
                        HistoryPos = -1;
            }

            // A better implementation would preserve the data on the current input line along with cursor position.
            if (prev_history_pos != HistoryPos)
            {
                const char* history_str = (HistoryPos >= 0) ? History[HistoryPos] : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, history_str);
            }
        }
        }
        return 0;
    }
};

static ExampleAppConsole console;
static void ShowExampleAppConsole(bool* p_open)
{
    console.Draw("Archipelago Client");
}

void set_imgui_style() noexcept {
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;
    style.WindowBorderSize = 2.0f;
    style.WindowPadding = ImVec2(2.0f, 0.0f);

    auto& colors = ImGui::GetStyle().Colors;
    // Window BG
    colors[ImGuiCol_WindowBg] = ImVec4{ 0.1f, 0.105f, 0.11f, 1.0f };

    // Navigatation highlight
    colors[ImGuiCol_NavHighlight] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };

    // Headers
    colors[ImGuiCol_Header] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_HeaderHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_HeaderActive] = ImVec4{ 0.55f, 0.5505f, 0.551f, 1.0f };

    // Buttons
    colors[ImGuiCol_Button] = ImVec4{ 0.2f, 0.205f, 0.21f, 1.0f };
    colors[ImGuiCol_ButtonHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_ButtonActive] = ImVec4{ 0.55f, 0.5505f, 0.551f, 1.0f };

    // Checkbox
    colors[ImGuiCol_CheckMark] = ImVec4(0.55f, 0.5505f, 0.551f, 1.0f);

    // Frame BG
    colors[ImGuiCol_FrameBg] = ImVec4{ 0.211f, 0.210f, 0.25f, 1.0f };
    colors[ImGuiCol_FrameBgHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_FrameBgActive] = ImVec4{ 0.55f, 0.5505f, 0.551f, 1.0f };

    // Tabs
    colors[ImGuiCol_Tab] = ImVec4{ 0.25f, 0.2505f, 0.251f, 1.0f };
    colors[ImGuiCol_TabHovered] = ImVec4{ 0.38f, 0.3805f, 0.381f, 1.0f };
    colors[ImGuiCol_TabActive] = ImVec4{ 0.28f, 0.2805f, 0.281f, 1.0f };
    colors[ImGuiCol_TabUnfocused] = ImVec4{ 0.25f, 0.2505f, 0.251f, 1.0f };
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4{ 0.8f, 0.805f, 0.81f, 1.0f };

    // Resize Grip
    colors[ImGuiCol_ResizeGrip] = ImVec4{ 0.2f, 0.205f, 0.21f, 0.0f };
    colors[ImGuiCol_ResizeGripHovered] = ImVec4{ 0.3f, 0.305f, 0.31f, 1.0f };
    colors[ImGuiCol_ResizeGripActive] = ImVec4{ 0.55f, 0.5505f, 0.551f, 1.0f };

    // Title
    colors[ImGuiCol_TitleBg] = ImVec4{ 0.25f, 0.2505f, 0.251f, 1.0f };
    colors[ImGuiCol_TitleBgActive] = ImVec4{ 0.55f, 0.5505f, 0.551f, 1.0f };
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4{ 0.25f, 0.2505f, 0.251f, 1.0f };

    const auto& fonts = ImGui::GetIO().Fonts;

    fonts->Clear();
    fonts->AddFontFromMemoryCompressedTTF(RobotoMedium_compressed_data, RobotoMedium_compressed_size, 16.0f);
    fonts->Build();
}

bool initialize_imgui() {
    if (g_initialized) {
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui::GetIO().IniFilename = "archipelago_client_ui.ini";

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

    set_imgui_style();

    g_initialized = true;
    return true;
}



#pragma endregion

#pragma region ArchipelagoFunctions
sol::table JsonToTable(const json& data) {
    API::LuaLock _{};
    sol::state_view lua{ g_lua };
    sol::table tab(lua, sol::create);
    for (auto& it : data.items()) {
        if (it.value().is_object() || it.value().is_array()) {
            tab.set(it.key(), JsonToTable(it.value()));
        }
        else if (it.value().is_boolean())
            tab.set(it.key(), it.value().get<bool>());
        else if (it.value().is_number_integer())
            tab.set(it.key(), it.value().get<int>());
        else if (it.value().is_number_float())
            tab.set(it.key(), it.value().get<float>());
        else if (it.value().is_string())
            tab.set(it.key(), it.value().get<std::string>());
    }
    return tab;
}

bool APSay(std::string msg) {
    if (AP == nullptr || !(AP->get_state() == APClient::State::SLOT_CONNECTED)) return false;
    return AP->Say(msg);

}

bool APIsConnected() {
    return AP != nullptr && (AP->get_state() > APClient::State::SOCKET_CONNECTED);
}

bool APGameComplete() {
    return AP != nullptr && AP->StatusUpdate(APClient::ClientStatus::GOAL);
}

std::string APGetItemName(int item_id) {
    if (AP == nullptr) return "";
    return AP->get_item_name(item_id);
}

std::string APGetLocationName(int location_id) {
    if (AP == nullptr) return "";
    return AP->get_location_name(location_id);
}

std::string APGetPlayerAlias(int player_id) {
    if (AP == nullptr) return "Unknown Player";
    return AP->get_player_alias(player_id);
}

std::string APGetSeed() {
    if (AP == nullptr) return "";
    return AP->get_seed();
}

std::string APGetSlot() {
    if (AP == nullptr) return "";
    return AP->get_slot();
}

int APGetPlayerNumber() {
    if (AP == nullptr) return -1;
    return AP->get_player_number();
}

int APGetTeamNumber() {
    if (AP == nullptr) return -1;
    return AP->get_team_number();
}

int APLocationChecks(sol::table table) {
    if (AP == nullptr) return -1;
    std::list<int64_t> locations;
    for (int i = 1; i < table.size(); i++) {
        int location = table[i];
        locations.push_back(location);
    }
    return AP->LocationChecks(locations);
}

int APLocationScouts(sol::table table, int create_as_hint = 0) {
    if (AP == nullptr) return -1;
    std::list<int64_t> locations;
    for (int i = 1; i < table.size(); i++) {
        int64_t location = table[i];
        locations.push_back(location);
    }
    return AP->LocationScouts(locations, create_as_hint);
}

int APGetData(sol::table table) {
    if (AP == nullptr) return -1;
    std::list <std::string> keys;
    for (int i = 1; i < table.size(); i++) {
        std::string str = table[i];
        keys.push_back(str);
    }
    return AP->Get(keys);
}

int APSetData(sol::table table) {
    if (AP == nullptr) return -1;
    std::string key = table["key"];
    auto default_val = table["default"];
    bool want_reply = table["want_reply"];
    std::list<APClient::DataStorageOperation> operations;
    sol::table opTable = table["operations"];
    for (int i = 1; i < table.size(); i++) {
        APClient::DataStorageOperation* op = new APClient::DataStorageOperation();
        op->operation = opTable[i]["operation"];
        op->value = opTable[i]["value"];
        operations.push_back(*op);
    }
    return AP->Set(key,default_val, want_reply, operations);
}

int APSetNotify(sol::table table) {
    if (AP == nullptr) return -1;
    std::list <std::string> keys;
    for (int i = 1; i < table.size(); i++) {
        std::string str = table[i];
        keys.push_back(str);
    }
    return AP->SetNotify(keys);
}

bool ConnectAP(std::string uri) {
    try {
        API::LuaLock _{};
        sol::state_view lua{ g_lua };

        std::string uuid = ap_get_uuid(UUID_FILE);

        if (AP) delete AP;
        AP = nullptr;

        if (!uri.empty() && uri.find("ws://") != 0 && uri.find("wss://") != 0) uri = "ws://" + uri;
        std::string game = lua["APGameName"];
        if (!game.empty()) {
            isWithoutRando = false;
        }
        console.AddLog("Connecting to AP...");
        if (uri.empty()) AP = new APClient(uuid, game);
        else AP = new APClient(uuid, game, uri);
        AP->set_socket_connected_handler([]() {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            lua["APSocketConnectedHandler"]();
            });
        AP->set_socket_disconnected_handler([]() {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            lua["APSocketDisconnectedHandler"]();
            });
        AP->set_slot_connected_handler([](const json& data) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            lua["APSlotConnectedHandler"](JsonToTable(data));
            });
        AP->set_room_info_handler([]() {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            std::list<std::string>* tags = new std::list<std::string>();
            tags->push_back("AP");
            if (isWithoutRando) {
                tags->push_back("TextOnly");
            }
            if (!AP->ConnectSlot(std::string(console.UserBuf), std::string(console.PassBuf), 7, *tags, {0,3,5})) {
                console.AddLog("Failed to connect to the slot.");
            }
            }
        );
        AP->set_items_received_handler([](const std::list<APClient::NetworkItem>& data) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            sol::table tab(lua, sol::create);
            int i = 1;
            for (auto it : data)
            {
                sol::table table(lua, sol::create);
                table.set(
                    "location", it.location,
                    "item", it.item,
                    "player", it.player,
                    "index", it.index,
                    "flags", it.flags);
                tab.set(i, table);
                i++;
            }
            lua["APItemsReceivedHandler"](tab);
            });
        AP->set_location_checked_handler([](const std::list<int64_t>& data) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            sol::table tab(lua, sol::create);
            int i = 1;
            for (auto it : data)
            {
                tab.set(i, it);
                i++;
            }
            lua["APLocationsCheckedHandler"](tab);
            });
        AP->set_location_info_handler([](const std::list<APClient::NetworkItem>& data) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            sol::table tab(lua, sol::create);
            int i = 1;
            for (auto it : data)
            {
                sol::table table(lua, sol::create);
                table.set(
                    "item", it.item,
                    "location", it.location,
                    "player", it.player,
                    "flags", it.flags);
                tab.set(i, it);
                i++;
            }
            lua["APLocationInfoHandler"](tab);
            }
        );
        AP->set_retrieved_handler([](const std::map<std::string, json> data) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            sol::table tab(lua, sol::create);
            for (auto pair : data) {
                tab.set(pair.first, JsonToTable(pair.second));
            }
            lua["APRetrievedHandler"](tab);
            });
        AP->set_set_reply_handler([](const std::string key, const json& value, const json& original_value) {
            // convert the two jsons and then just pass to lua
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            lua["APSetReplyHandler"](key, JsonToTable(value), JsonToTable(original_value));
            });
        AP->set_data_package_changed_handler([](const json& data) {
            AP->save_data_package(DATAPACKAGE_CACHE);
            });
        AP->set_print_handler([](const std::string& msg) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            console.AddLog(msg.c_str());
            lua["APPrintHandler"](msg);
            });

        AP->set_print_json_handler([](const std::list<APClient::TextNode>& msg) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            console.AddLog(AP->render_json(msg, APClient::RenderFormat::TEXT).c_str());
            sol::table tab(lua, sol::create);
            int i = 1;
            for(auto it : msg)
            {
                sol::table table(lua, sol::create);
                table.set(
                    "type", it.type.c_str(),
                    "color", it.color.c_str(),
                    "player", it.player,
                    "text", it.text.c_str(),
                    "flags", it.flags);
                tab.set(i, table);
                i++;
            }
            lua["APPrintJSONHandler"](tab);
            });
        AP->set_bounced_handler([](const json& data) {
            API::LuaLock _{};
            sol::state_view lua{ g_lua };
            lua["APBouncedHandler"](JsonToTable(data));
            });
        return true;
    }
    catch (std::string error) {
        console.AddLog(error.c_str());
        return false;
    }

}
#pragma endregion


#pragma region RandomizationFunctions

std::string random_string(size_t length)
{
    auto randchar = []() -> char
    {
        const char charset[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        const size_t max_index = (sizeof(charset) - 1);
        return charset[rand() % max_index];
    };
    std::string str(length, 0);
    std::generate_n(str.begin(), length, randchar);
    return str;
}//copied from StackOverflow, by user Carl

void SetSpecificSeed(std::string seed) {
    std::seed_seq seedseq(seed.begin(), seed.end());
    randgen.seed(seedseq);
}

void SetRandomSeed() {
    std::string seed = random_string(32);
    SetSpecificSeed(seed);
}

int UniformRandomInteger(int min, int max) {
    std::uniform_int_distribution<int> gen(min, max);
    int rand = gen(randgen);
    return rand;
}

int NormalRandomInteger(double mu, double sigma) {
    //we don't check bounds here, so the user must check for bounds themself
    std::normal_distribution<double> gen(mu, sigma);

    int rand = int(gen(randgen));
    return rand;
}

#pragma endregion

#pragma region REFFunctions
void on_lua_state_created(lua_State* l) {
    API::LuaLock _{};

    g_lua = l;
    g_loaded_snippets.clear();

    sol::state_view lua{ g_lua };

    // adds a new function to call from lua!
    lua["APGameName"] = ""; //Default to allow for calling as a text client, and ensure connection doesn't immediately fail
    lua["APSay"] = APSay;
    lua["APGameComplete"] = APGameComplete;
    lua["APIsConnected"] = APIsConnected;
    lua["APGetItemName"] = APGetItemName;
    lua["APGetLocationName"] = APGetLocationName;
    lua["APGetPlayerAlias"] = APGetPlayerAlias;
    lua["APGetPlayerNumber"] = APGetPlayerNumber;
    lua["APGetTeamNumber"] = APGetTeamNumber;
    lua["APGetSlot"] = APGetSlot;
    lua["APGetSeed"] = APGetSeed;
    lua["APLocationChecks"] = APLocationChecks;
    lua["APLocationScouts"] = APLocationScouts;
    lua["APGetData"] = APGetData;
    lua["APSetData"] = APSetData;
    lua["APSetNotify"] = APSetNotify;
    lua["SetRandomSeed"] = SetRandomSeed;
    lua["SetSpecificSeed"] = SetSpecificSeed;
    lua["UniformRandomInteger"] = UniformRandomInteger;
    lua["NormalRandomInteger"] = NormalRandomInteger;
}

void on_lua_state_destroyed(lua_State* l) {
    API::LuaLock _{};

    g_lua = nullptr;
    g_loaded_snippets.clear();
}


void on_present() {
    if (!g_initialized) {
        if (!initialize_imgui()) {
            return;
        }
    }

    const auto renderer_data = API::get()->param()->renderer_data;

    if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D11) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ShowExampleAppConsole(&isOpen);

        ImGui::EndFrame();
        ImGui::Render();

        g_d3d11.render_imgui();
    }
    else if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12) {
        auto command_queue = (ID3D12CommandQueue*)renderer_data->command_queue;

        if (command_queue == nullptr) {
            return;
        }

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ShowExampleAppConsole(&isOpen);

        ImGui::EndFrame();
        ImGui::Render();

        g_d3d12.render_imgui();
    }
}


void on_device_reset() {
    const auto renderer_data = API::get()->param()->renderer_data;

    if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D11) {
        ImGui_ImplDX11_Shutdown();
        g_d3d11 = {};
    }

    if (renderer_data->renderer_type == REFRAMEWORK_RENDERER_D3D12) {
        ImGui_ImplDX12_Shutdown();
        g_d3d12 = {};
    }

    g_initialized = false;
}

bool on_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);

    return !ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard;
}

void update_ap() {
    if (AP) AP->poll();
}

extern "C" __declspec(dllexport) void reframework_plugin_required_version(REFrameworkPluginVersion * version) {
    version->major = REFRAMEWORK_PLUGIN_VERSION_MAJOR;
    version->minor = REFRAMEWORK_PLUGIN_VERSION_MINOR;
    version->patch = REFRAMEWORK_PLUGIN_VERSION_PATCH;

    // Optionally, specify a specific game name that this plugin is compatible with.
    version->game_name = "MHRISE";
}

extern "C" __declspec(dllexport) bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam * param) {
    API::initialize(param);
    ImGui::CreateContext();
    std::srand(time(NULL));//for seed generation
    const auto functions = param->functions;
    functions->on_lua_state_created(on_lua_state_created);
    functions->on_lua_state_destroyed(on_lua_state_destroyed);
    functions->on_present(on_present);
    functions->on_device_reset(on_device_reset);
    functions->on_message((REFOnMessageCb)on_message);
    functions->on_pre_application_entry("BeginRendering", update_ap);

    return true;
}
#pragma endregion

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

