#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <zlib.h>
#include "minIni.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dwmapi.lib")

#define WINDOW_NAME "R6 Config | @gottvonterrorismus"
#define WIDTH 430
#define HEIGHT 200

namespace fs = std::filesystem;

static constexpr uint32_t CONFIG_MAGIC   = 0x52364346; // "R6CF"
static constexpr uint8_t  CONFIG_VERSION = 1;

#pragma pack(push, 1)
struct ConfigHeader {
    uint32_t magic;
    uint8_t  version;
    uint32_t crc32;
};

struct GameConfig {
    // DISPLAY
    float brightness;

    // DISPLAY_SETTINGS
    int   aspectRatio;
    float fov;

    // AUDIO
    int dynamicRange;

    // ACCESSIBILITY
    int acm;
    int pci;
    int nci;
    int oci;
    int pingColorIndex;
    int tcai;
    int tcei;
    int stunVfx;
    int tinnitusSFX;

    // INPUT
    int   sensX;
    int   sensY;
    float multiplier;

    int customADS;
    int adsGlobal;
    int ads1x;
    int ads2xHalf;
    int ads3x;
    int ads4x;
    int ads5x;
    int ads12x;
};
#pragma pack(pop)

enum class FieldType { Int, Float };

struct FieldDef {
    const char* section;
    const char* key;
    size_t      offset;
    FieldType   type;
};

#define FIELD_INT(sec, key, member)   { sec, key, offsetof(GameConfig, member), FieldType::Int   }
#define FIELD_FLOAT(sec, key, member) { sec, key, offsetof(GameConfig, member), FieldType::Float }

static const FieldDef k_fields[] = {
    FIELD_FLOAT("DISPLAY",          "Brightness",                    brightness),
    FIELD_INT  ("DISPLAY_SETTINGS", "AspectRatio",                   aspectRatio),
    FIELD_FLOAT("DISPLAY_SETTINGS", "DefaultFOV",                    fov),
    FIELD_INT  ("AUDIO",            "DynamicRangeMode",              dynamicRange),
    FIELD_INT  ("ACCESSIBILITY",    "AccessibilityColorMode",        acm),
    FIELD_INT  ("ACCESSIBILITY",    "PositiveColorIndex",            pci),
    FIELD_INT  ("ACCESSIBILITY",    "NegativeColorIndex",            nci),
    FIELD_INT  ("ACCESSIBILITY",    "ObjectiveColorIndex",           oci),
    FIELD_INT  ("ACCESSIBILITY",    "PingColorIndex",                pingColorIndex),
    FIELD_INT  ("ACCESSIBILITY",    "TeamColorAllyIndex",            tcai),
    FIELD_INT  ("ACCESSIBILITY",    "TeamColorEnemyIndex",           tcei),
    FIELD_INT  ("ACCESSIBILITY",    "StunVFXMode",                   stunVfx),
    FIELD_INT  ("ACCESSIBILITY",    "TinnitusSFXMode",               tinnitusSFX),
    FIELD_INT  ("INPUT",            "MouseYawSensitivity",           sensX),
    FIELD_INT  ("INPUT",            "MousePitchSensitivity",         sensY),
    FIELD_FLOAT("INPUT",            "MouseSensitivityMultiplierUnit",multiplier),
    FIELD_INT  ("INPUT",            "ADSMouseUseSpecific",           customADS),
    FIELD_INT  ("INPUT",            "ADSMouseSensitivityGlobal",     adsGlobal),
};

static const FieldDef k_adsFields[] = {
    FIELD_INT("INPUT", "ADSMouseSensitivity1x",     ads1x),
    FIELD_INT("INPUT", "ADSMouseSensitivity2xHalf", ads2xHalf),
    FIELD_INT("INPUT", "ADSMouseSensitivity3x",     ads3x),
    FIELD_INT("INPUT", "ADSMouseSensitivity4x",     ads4x),
    FIELD_INT("INPUT", "ADSMouseSensitivity5x",     ads5x),
    FIELD_INT("INPUT", "ADSMouseSensitivity12x",    ads12x),
};

#undef FIELD_INT
#undef FIELD_FLOAT

static constexpr char k_b64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static const auto k_b64Reverse = []() {
    std::array<uint8_t, 256> t{};
    const char* src = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; src[i]; i++) t[(uint8_t)src[i]] = (uint8_t)i;
    return t;
}();

std::string base64Encode(std::span<const uint8_t> data)
{
    size_t inputLen  = data.size();
    size_t outputLen = 4 * ((inputLen + 2) / 3);
    std::string out(outputLen, '\0');

    for(size_t i = 0, j = 0; i < inputLen; )
    {
        uint32_t a = i < inputLen ? data[i++] : 0;
        uint32_t b = i < inputLen ? data[i++] : 0;
        uint32_t c = i < inputLen ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[j++] = k_b64Table[(triple >> 18) & 0x3F];
        out[j++] = k_b64Table[(triple >> 12) & 0x3F];
        out[j++] = k_b64Table[(triple >> 6)  & 0x3F];
        out[j++] = k_b64Table[ triple        & 0x3F];
    }

    for(size_t i = 0; i < (3 - inputLen % 3) % 3; i++)
        out[outputLen - 1 - i] = '=';

    return out;
}

std::optional<std::vector<uint8_t>> base64Decode(std::string_view data)
{
    if(data.size() % 4 != 0) return std::nullopt;

    size_t outputLen = data.size() / 4 * 3;
    if(data[data.size() - 1] == '=') outputLen--;
    if(data[data.size() - 2] == '=') outputLen--;

    std::vector<uint8_t> out(outputLen);

    for(size_t i = 0, j = 0; i < data.size(); )
    {
        uint32_t a = data[i] == '=' ? 0 : k_b64Reverse[(uint8_t)data[i]]; i++;
        uint32_t b = data[i] == '=' ? 0 : k_b64Reverse[(uint8_t)data[i]]; i++;
        uint32_t c = data[i] == '=' ? 0 : k_b64Reverse[(uint8_t)data[i]]; i++;
        uint32_t d = data[i] == '=' ? 0 : k_b64Reverse[(uint8_t)data[i]]; i++;

        uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;

        if(j < outputLen) out[j++] = (triple >> 16) & 0xFF;
        if(j < outputLen) out[j++] = (triple >> 8)  & 0xFF;
        if(j < outputLen) out[j++] =  triple        & 0xFF;
    }

    return out;
}

class IniFile {
public:
    explicit IniFile(fs::path path) : m_path(std::move(path)) {}

    void writeFields(const GameConfig& cfg, std::span<const FieldDef> fields) const
    {
        for(const auto& f : fields)
        {
            const void* ptr = reinterpret_cast<const uint8_t*>(&cfg) + f.offset;
            if(f.type == FieldType::Int)
                ini_putl(f.section, f.key, *static_cast<const int*>(ptr),   m_path.string().c_str());
            else
                ini_putf(f.section, f.key, *static_cast<const float*>(ptr), m_path.string().c_str());
        }
    }

    void readFields(GameConfig& cfg, std::span<const FieldDef> fields) const
    {
        for(const auto& f : fields)
        {
            void* ptr = reinterpret_cast<uint8_t*>(&cfg) + f.offset;
            if(f.type == FieldType::Int)
                *static_cast<int*>(ptr)   = (int)ini_getl(f.section, f.key, 0,    m_path.string().c_str());
            else
                *static_cast<float*>(ptr) = (float)ini_getf(f.section, f.key, 0.f, m_path.string().c_str());
        }
    }

private:
    fs::path m_path;
};

std::vector<fs::path> getProfiles()
{
    const char* username = std::getenv("USERNAME");

    fs::path base = fs::path("C:\\Users") / username / "Documents\\My Games\\Rainbow Six - Siege";

    std::error_code ec;
    if(!fs::exists(base, ec))
        return {};

    std::vector<fs::path> profiles;
    for(const auto& entry : fs::directory_iterator(base, ec))
    {
        if(!entry.is_directory()) continue;
        if(entry.path().filename() == "Benchmark") continue;
        profiles.push_back(entry.path());
    }

    if(profiles.empty())
        return {};

    return profiles;
}

static int doImport(const IniFile& ini, std::string_view configStr)
{
    auto compressed = base64Decode(configStr);
    if(!compressed)
        return 1;

    constexpr size_t payloadSize = sizeof(ConfigHeader) + sizeof(GameConfig);
    std::vector<uint8_t> payload(payloadSize);

    uLongf decompressedSize = (uLongf)payloadSize;
    if(uncompress(payload.data(), &decompressedSize, compressed->data(), (uLong)compressed->size()) != Z_OK)
        return 2;

    if(decompressedSize != payloadSize)
        return 3;

    const auto& header = *reinterpret_cast<const ConfigHeader*>(payload.data());
    const auto& config = *reinterpret_cast<const GameConfig*>(payload.data() + sizeof(ConfigHeader));

    if(header.magic != CONFIG_MAGIC)
        return 4;

    if(header.version != CONFIG_VERSION)
        return 5;

    uint32_t computedCRC = (uint32_t)crc32(0L, (const Bytef*)&config, sizeof(GameConfig));
    if(computedCRC != header.crc32)
        return 6;

    ini.writeFields(config, k_fields);
    if(config.customADS)
        ini.writeFields(config, k_adsFields);

    return 0;
}

std::string doExport(const IniFile& ini)
{
    GameConfig config{};
    ini.readFields(config, k_fields);
    if(config.customADS)
        ini.readFields(config, k_adsFields);

    ConfigHeader header{};
    header.magic   = CONFIG_MAGIC;
    header.version = CONFIG_VERSION;
    header.crc32   = (uint32_t)crc32(0L, (const Bytef*)&config, sizeof(GameConfig));

    constexpr size_t payloadSize = sizeof(ConfigHeader) + sizeof(GameConfig);
    std::vector<uint8_t> payload(payloadSize);
    std::memcpy(payload.data(),                    &header, sizeof(ConfigHeader));
    std::memcpy(payload.data() + sizeof(ConfigHeader), &config, sizeof(GameConfig));

    uLongf compressedBound = compressBound((uLong)payloadSize);
    std::vector<uint8_t> compressed(compressedBound);

    if(compress(compressed.data(), &compressedBound, payload.data(), (uLong)payloadSize) != Z_OK)
        return {};
    compressed.resize(compressedBound);

    std::string encoded = base64Encode(compressed);

    return encoded;
}

static ID3D11Device*            g_pd3dDevice            = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext     = nullptr;
static IDXGISwapChain*          g_pSwapChain            = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView  = nullptr;

HWND g_hwnd = nullptr;

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount          = 2;
    sd.BufferDesc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow         = hWnd;
    sd.SampleDesc.Count     = 1;
    sd.Windowed             = TRUE;
    sd.SwapEffect           = DXGI_SWAP_EFFECT_DISCARD;

    if(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &g_pSwapChain, &g_pd3dDevice,
            nullptr, &g_pd3dDeviceContext) != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupRenderTarget()
{
    if(g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if(g_pSwapChain)        { g_pSwapChain->Release();        g_pSwapChain        = nullptr; }
    if(g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if(g_pd3dDevice)        { g_pd3dDevice->Release();        g_pd3dDevice        = nullptr; }
}

static bool CursorInImGuiTitleBar()
{
    ImGuiWindow* w = ImGui::FindWindowByName(WINDOW_NAME);
    if(!w) return false;

    ImRect tb = w->TitleBarRect();

    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);

    float closeButtonX = tb.Max.x - w->TitleBarHeight;
    if(pt.x >= closeButtonX) return false;

    return (pt.x >= tb.Min.x && pt.x < tb.Max.x &&
            pt.y >= tb.Min.y && pt.y < tb.Max.y);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch(msg)
    {
    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
        if(hit == HTCLIENT && CursorInImGuiTitleBar())
            return HTCAPTION;
        return hit;
    }

    case WM_SIZE:
        if(g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0,
                (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int CopyToClipboard(const std::string& text)
{
    if(!OpenClipboard(nullptr)) return 0;

    if(!EmptyClipboard())
    {
        CloseClipboard();
        return 0;
    }

    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if(!hGlob)
    {
        CloseClipboard();
        return 0;
    }

    void* pData = GlobalLock(hGlob);
    if(!pData)
    {
        GlobalFree(hGlob);
        CloseClipboard();
        return 0;
    }

    memcpy(pData, text.c_str(), text.size() + 1);
    GlobalUnlock(hGlob);

    if(!SetClipboardData(CF_TEXT, hGlob))
    {
        GlobalFree(hGlob);
        CloseClipboard();
        return 0;
    }

    CloseClipboard();
    return 1;
}

void showToast(const std::string& text, ImVec4 color, float duration = 3.0f)
{
    static std::string  s_text;
    static ImVec4       s_color;
    static float        s_timeLeft = 0.0f;

    if(!text.empty())
    {
        s_text     = text;
        s_color    = color;
        s_timeLeft = duration;
    }

    if(s_timeLeft <= 0.0f) return;

    s_timeLeft -= ImGui::GetIO().DeltaTime;

    ImVec2 windowPos  = ImGui::GetWindowPos();
    ImVec2 windowSize = ImGui::GetWindowSize();
    ImVec2 textSize   = ImGui::CalcTextSize(s_text.c_str());

    ImVec2 textPos = {
        windowPos.x + 8.0f,
        windowPos.y + windowSize.y - textSize.y - 8.0f
    };

    ImGui::GetWindowDrawList()->AddText(textPos, ImGui::ColorConvertFloat4ToU32(s_color), s_text.c_str());
}

void HelpMarker(const char *desc, std::optional<bool> formatted = false)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if(ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        if(!formatted)
            ImGui::TextUnformatted(desc);
        else ImGui::Text(desc);
        ImGui::EndTooltip();
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    WNDCLASSEX wc = {
        sizeof(WNDCLASSEX), CS_CLASSDC, WndProc,
        0L, 0L, hInstance,
        nullptr, nullptr, nullptr, nullptr,
        "ImGuiBorderless", nullptr
    };
    RegisterClassEx(&wc);

    g_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "R6 Config",
        WS_POPUP | WS_VISIBLE,
        100, 100, WIDTH, HEIGHT,
        nullptr, nullptr,
        wc.hInstance, nullptr
    );

    BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(g_hwnd,
        DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableTransitions, sizeof(disableTransitions));

    if(!CreateDeviceD3D(g_hwnd))
        return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    ShowWindow(g_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_hwnd);

    bool open = true;

    MSG msg = {};
    while(msg.message != WM_QUIT && open)
    {
        while(PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if(msg.message == WM_QUIT) goto done;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);

        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoResize   |
            ImGuiWindowFlags_NoMove     |
            ImGuiWindowFlags_NoCollapse;

        ImGui::Begin(WINDOW_NAME, &open, flags);

        static std::vector<fs::path> profiles = getProfiles();
        static int selected_profile = 0;
        auto getter = [](void* data, int idx, const char** out) -> bool {
            auto& p = *static_cast<std::vector<fs::path>*>(data);
            if (idx < 0 || idx >= (int)p.size()) return false;
            static std::string label;
            label = p[idx].filename().string();
            *out = label.c_str();
            return true;
        };
        ImGui::Combo("Profile", &selected_profile, getter, &profiles, (int)profiles.size());
        HelpMarker("This is your profile ID. If you have multiple accounts,\nyou will need to find the right one.");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        if(ImGui::BeginTabBar("tabs"))
        {
            if(ImGui::BeginTabItem("Import"))
            {
                ImGui::Spacing();

                static char config_str[4096] = "";

                if(ImGui::Button("Import"))
                {
                    if(strlen(config_str) > 30)
                    {
                        IniFile ini(profiles[selected_profile] / "GameSettings.ini");
                        int result = doImport(ini, config_str);
                        if(!result)
                        {
                            showToast("Config imported.", {0.3f, 1.0f, 0.3f, 1.0f});
                        }
                        else if(result == 1)
                        {
                            showToast("Failed to decode config.", {1.0f, 0.3f, 0.3f, 1.0f});
                        }
                        else if(result == 2)
                        {
                            showToast("Failed to decompress config.", {1.0f, 0.3f, 0.3f, 1.0f});
                        }
                        else if(result == 3)
                        {
                            showToast("Decompressed size =/= payload size", {1.0f, 0.3f, 0.3f, 1.0f});
                        }
                        else if(result == 4)
                        {
                            showToast("Config doesn't contain header magic.", {1.0f, 0.3f, 0.3f, 1.0f});
                        }
                        else if(result == 5)
                        {
                            showToast("Config version doesn't match app version.", {1.0f, 0.3f, 0.3f, 1.0f});
                        }
                        else if(result == 6)
                        {
                            showToast("Config is corrupted.", {1.0f, 0.3f, 0.3f, 1.0f});
                        }
                        
                    }
                    else showToast("Invalid config.", {1.0f, 0.3f, 0.3f, 1.0f});
                }

                ImGui::SameLine();

                ImGui::InputTextWithHint("Config", "Enter your code...", config_str, IM_ARRAYSIZE(config_str));
                HelpMarker("Paste the code shared by another player here.");

                ImGui::EndTabItem();
            }
            static bool exported = false;
            static std::string config_str;
            if(ImGui::BeginTabItem("Export"))
            {
                ImGui::Spacing();
                if(ImGui::Button("Export"))
                {
                    IniFile ini(profiles[selected_profile] / "GameSettings.ini");
                    config_str = doExport(ini);
                    if(!config_str.empty())
                    {
                        exported = true;
                        showToast("Config exported.", {0.3f, 1.0f, 0.3f, 1.0f});
                    }
                }

                if(exported)
                {
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::Text("Exported config: %.30s...", config_str.c_str());
                    ImGui::SameLine();
                    if(ImGui::Button("Copy"))
                    {
                        CopyToClipboard(config_str);
                        showToast("Config copied.", {0.3f, 1.0f, 0.3f, 1.0f});
                    }
                }

                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("Help"))
            {
                ImGui::Text("If you're having trouble finding the correct ID, just apply\nthe config to all of them.");
                ImGui::Separator();
                ImGui::Text("If your Profiles list is empty, the game is not installed.");
                ImGui::EndTabItem();
            }
            if(ImGui::BeginTabItem("About"))
            {
                ImGui::Text("Made by: @gottvonterrorismus");
                ImGui::Spacing();
                ImGui::Text("Version: v1.2");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        showToast("", {});

        ImGui::End();

        ImGui::Render();

        const float cc[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);
    }

done:
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);
    return 0;
}
