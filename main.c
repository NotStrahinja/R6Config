#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <zlib.h>
#include <stdint.h>
#include "minIni.h"
#include "webview/webview.h"
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ui_html.h"

#define CONFIG_VERSION  2
#define CONFIG_MAGIC    0x52364346  // "R6CF"

/* exportMask bits */
#define EXPORT_DISPLAY        (1 << 0)
#define EXPORT_DISPLAY_SET    (1 << 1)
#define EXPORT_AUDIO          (1 << 2)
#define EXPORT_ACCESSIBILITY  (1 << 3)
#define EXPORT_INPUT          (1 << 4)
#define EXPORT_ALL            (EXPORT_DISPLAY | EXPORT_DISPLAY_SET | EXPORT_AUDIO | EXPORT_ACCESSIBILITY | EXPORT_INPUT)

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint8_t  version;
    uint32_t crc32;
    uint8_t  exportMask;
} ConfigHeader;

typedef struct {
    // DISPLAY
    float brightness;

    // DISPLAY_SETTINGS
    int aspectRatio;
    float fov;

    // AUDIO
    int dynamicRange;

    // ACCESSIBILITY
    int acm;
    int pci;
    int nci;
    int oci;
    int pci2;
    int tcai;
    int tcei;
    int stunVfx;
    int tinnitusSFX;

    // INPUT
    int sensX;
    int sensY;
    float multiplier;

    int customADS;
    int adsGlobal;
    int ads1x;
    int ads2xHalf;
    int ads3x;
    int ads4x;
    int ads5x;
    int ads12x;

} GameConfig;
#pragma pack(pop)

/* ------------------------------------------------------------------ */
/* base64                                                              */
/* ------------------------------------------------------------------ */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64_encode(const unsigned char* data, size_t input_length, size_t* output_length)
{
    *output_length = 4 * ((input_length + 2) / 3);

    char* encoded = malloc(*output_length + 1);
    if(!encoded) return NULL;

    for(size_t i = 0, j = 0; i < input_length;)
    {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded[j++] = b64_table[(triple >> 18) & 0x3F];
        encoded[j++] = b64_table[(triple >> 12) & 0x3F];
        encoded[j++] = b64_table[(triple >> 6) & 0x3F];
        encoded[j++] = b64_table[triple & 0x3F];
    }

    for(size_t i = 0; i < (3 - input_length % 3) % 3; i++)
        encoded[*output_length - 1 - i] = '=';

    encoded[*output_length] = '\0';
    return encoded;
}

static const unsigned char b64_reverse_table[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,
    ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,['h']=33,
    ['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,['o']=40,['p']=41,
    ['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,['w']=48,['x']=49,
    ['y']=50,['z']=51,
    ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,
    ['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
};

unsigned char* base64_decode(const char* data, size_t input_length, size_t* output_length)
{
    if(input_length % 4 != 0) return NULL;

    *output_length = input_length / 4 * 3;
    if(data[input_length - 1] == '=') (*output_length)--;
    if(data[input_length - 2] == '=') (*output_length)--;

    unsigned char* decoded = malloc(*output_length);
    if(!decoded) return NULL;

    for(size_t i = 0, j = 0; i < input_length;)
    {
        uint32_t sextet_a = data[i] == '=' ? 0 : b64_reverse_table[(unsigned char)data[i]]; i++;
        uint32_t sextet_b = data[i] == '=' ? 0 : b64_reverse_table[(unsigned char)data[i]]; i++;
        uint32_t sextet_c = data[i] == '=' ? 0 : b64_reverse_table[(unsigned char)data[i]]; i++;
        uint32_t sextet_d = data[i] == '=' ? 0 : b64_reverse_table[(unsigned char)data[i]]; i++;

        uint32_t triple = (sextet_a << 18)
                        | (sextet_b << 12)
                        | (sextet_c << 6)
                        | sextet_d;

        if(j < *output_length) decoded[j++] = (triple >> 16) & 0xFF;
        if(j < *output_length) decoded[j++] = (triple >> 8) & 0xFF;
        if(j < *output_length) decoded[j++] = triple & 0xFF;
    }

    return decoded;
}

/* ------------------------------------------------------------------ */
/* profile discovery                                                   */
/* ------------------------------------------------------------------ */

#define MAX_PROFILES 20

static char g_documentsPath[512];
static char g_profiles[MAX_PROFILES][256];
static int  g_profileCount = 0;

static int load_profiles(void)
{
#ifndef _DEBUG
    char *username = getenv("USERNAME");
    if(!username) username = "Computer";
#else
    char *username = "Computer";
#endif

    snprintf(g_documentsPath, sizeof(g_documentsPath),
             "C:\\Users\\%s\\Documents\\My Games\\Rainbow Six - Siege\\", username);

    DIR *d = opendir(g_documentsPath);
    if(!d) return 0;

    g_profileCount = 0;

    struct dirent *entry;
    while((entry = readdir(d)) != NULL && g_profileCount < MAX_PROFILES)
    {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        if(strcmp(entry->d_name, "Benchmark") == 0) continue;

        strncpy(g_profiles[g_profileCount], entry->d_name, sizeof(g_profiles[0]) - 1);
        g_profiles[g_profileCount][sizeof(g_profiles[0]) - 1] = '\0';
        g_profileCount++;
    }

    closedir(d);
    return g_profileCount;
}

static void get_settings_path(int idx, char *out, size_t out_size)
{
    snprintf(out, out_size, "%s%s\\GameSettings.ini", g_documentsPath, g_profiles[idx]);
}

/* ------------------------------------------------------------------ */
/* import / export                                                     */
/* ------------------------------------------------------------------ */

/* returns malloc'd base64 string on success, NULL on failure */
static char* do_export(const char* iniPath, uint8_t mask)
{
    GameConfig config;
    memset(&config, 0, sizeof(config));

    if(mask & EXPORT_DISPLAY)
    {
        config.brightness = ini_getf("DISPLAY", "Brightness", 0.0f, iniPath);
    }

    if(mask & EXPORT_DISPLAY_SET)
    {
        config.aspectRatio = ini_getl("DISPLAY_SETTINGS", "AspectRatio", 0, iniPath);
        config.fov         = ini_getf("DISPLAY_SETTINGS", "DefaultFOV", 0.0f, iniPath);
    }

    if(mask & EXPORT_AUDIO)
    {
        config.dynamicRange = ini_getl("AUDIO", "DynamicRangeMode", 0, iniPath);
    }

    if(mask & EXPORT_ACCESSIBILITY)
    {
        config.acm        = ini_getl("ACCESSIBILITY", "AccessibilityColorMode", 0, iniPath);
        config.pci        = ini_getl("ACCESSIBILITY", "PositiveColorIndex", 0, iniPath);
        config.nci        = ini_getl("ACCESSIBILITY", "NegativeColorIndex", 0, iniPath);
        config.oci        = ini_getl("ACCESSIBILITY", "ObjectiveColorIndex", 0, iniPath);
        config.pci2       = ini_getl("ACCESSIBILITY", "PingColorIndex", 0, iniPath);
        config.tcai       = ini_getl("ACCESSIBILITY", "TeamColorAllyIndex", 0, iniPath);
        config.tcei       = ini_getl("ACCESSIBILITY", "TeamColorEnemyIndex", 0, iniPath);
        config.stunVfx    = ini_getl("ACCESSIBILITY", "StunVFXMode", 0, iniPath);
        config.tinnitusSFX= ini_getl("ACCESSIBILITY", "TinnitusSFXMode", 0, iniPath);
    }

    if(mask & EXPORT_INPUT)
    {
        config.sensX      = ini_getl("INPUT", "MouseYawSensitivity", 0, iniPath);
        config.sensY      = ini_getl("INPUT", "MousePitchSensitivity", 0, iniPath);
        config.multiplier = ini_getf("INPUT", "MouseSensitivityMultiplierUnit", 0.0f, iniPath);

        config.customADS  = ini_getl("INPUT", "ADSMouseUseSpecific", 0, iniPath);
        config.adsGlobal  = ini_getl("INPUT", "ADSMouseSensitivityGlobal", 0, iniPath);

        if(config.customADS)
        {
            config.ads1x     = ini_getl("INPUT", "ADSMouseSensitivity1x", 0, iniPath);
            config.ads2xHalf = ini_getl("INPUT", "ADSMouseSensitivity2xHalf", 0, iniPath);
            config.ads3x     = ini_getl("INPUT", "ADSMouseSensitivity3x", 0, iniPath);
            config.ads4x     = ini_getl("INPUT", "ADSMouseSensitivity4x", 0, iniPath);
            config.ads5x     = ini_getl("INPUT", "ADSMouseSensitivity5x", 0, iniPath);
            config.ads12x    = ini_getl("INPUT", "ADSMouseSensitivity12x", 0, iniPath);
        }
    }

    ConfigHeader header;
    header.magic      = CONFIG_MAGIC;
    header.version    = CONFIG_VERSION;
    header.exportMask = mask;
    header.crc32      = (uint32_t)crc32(0L, (const Bytef*)&config, sizeof(GameConfig));

    size_t payloadSize = sizeof(ConfigHeader) + sizeof(GameConfig);
    unsigned char* payload = malloc(payloadSize);
    if(!payload) return NULL;

    memcpy(payload, &header, sizeof(ConfigHeader));
    memcpy(payload + sizeof(ConfigHeader), &config, sizeof(GameConfig));

    uLongf compressedSize = compressBound((uLong)payloadSize);
    unsigned char* compressed = malloc(compressedSize);
    if(!compressed)
    {
        free(payload);
        return NULL;
    }

    if(compress(compressed, &compressedSize, payload, (uLong)payloadSize) != Z_OK)
    {
        free(payload);
        free(compressed);
        return NULL;
    }

    free(payload);

    size_t encodedSize;
    char* encoded = base64_encode(compressed, compressedSize, &encodedSize);
    free(compressed);

    return encoded; /* caller frees */
}

/*
 * Result codes:
 *   0 = success
 *   1 = failed to decode base64
 *   2 = failed to decompress
 *   3 = decompressed size mismatch
 *   4 = invalid header magic
 *   5 = version mismatch
 *   6 = CRC32 mismatch (corrupted)
 *   7 = input too short / invalid
 */
static int do_import(const char* iniPath, const char* config_str)
{
    if(strlen(config_str) <= 30) return 7;

    size_t compressedSize;
    unsigned char* compressed = base64_decode(config_str, strlen(config_str), &compressedSize);
    if(!compressed) return 1;

    size_t payloadSize = sizeof(ConfigHeader) + sizeof(GameConfig);
    unsigned char* payload = malloc(payloadSize);
    if(!payload)
    {
        free(compressed);
        return 1;
    }

    uLongf decompressedSize = (uLongf)payloadSize;
    if(uncompress(payload, &decompressedSize, compressed, (uLong)compressedSize) != Z_OK)
    {
        free(compressed);
        free(payload);
        return 2;
    }

    free(compressed);

    if(decompressedSize != payloadSize)
    {
        free(payload);
        return 3;
    }

    ConfigHeader header;
    memcpy(&header, payload, sizeof(ConfigHeader));

    GameConfig config;
    memcpy(&config, payload + sizeof(ConfigHeader), sizeof(GameConfig));

    free(payload);

    if(header.magic != CONFIG_MAGIC) return 4;
    if(header.version != CONFIG_VERSION) return 5;

    uint32_t computedCRC = (uint32_t)crc32(0L, (const Bytef*)&config, sizeof(GameConfig));
    if(computedCRC != header.crc32) return 6;

    uint8_t mask = header.exportMask;

    if(mask & EXPORT_DISPLAY)
    {
        ini_putf("DISPLAY", "Brightness", config.brightness, iniPath);
    }

    if(mask & EXPORT_DISPLAY_SET)
    {
        ini_putl("DISPLAY_SETTINGS", "AspectRatio", config.aspectRatio, iniPath);
        ini_putf("DISPLAY_SETTINGS", "DefaultFOV", config.fov, iniPath);
    }

    if(mask & EXPORT_AUDIO)
    {
        ini_putl("AUDIO", "DynamicRangeMode", config.dynamicRange, iniPath);
    }

    if(mask & EXPORT_ACCESSIBILITY)
    {
        ini_putl("ACCESSIBILITY", "AccessibilityColorMode", config.acm, iniPath);
        ini_putl("ACCESSIBILITY", "PositiveColorIndex", config.pci, iniPath);
        ini_putl("ACCESSIBILITY", "NegativeColorIndex", config.nci, iniPath);
        ini_putl("ACCESSIBILITY", "ObjectiveColorIndex", config.oci, iniPath);
        ini_putl("ACCESSIBILITY", "PingColorIndex", config.pci2, iniPath);
        ini_putl("ACCESSIBILITY", "TeamColorAllyIndex", config.tcai, iniPath);
        ini_putl("ACCESSIBILITY", "TeamColorEnemyIndex", config.tcei, iniPath);
        ini_putl("ACCESSIBILITY", "StunVFXMode", config.stunVfx, iniPath);
        ini_putl("ACCESSIBILITY", "TinnitusSFXMode", config.tinnitusSFX, iniPath);
    }

    if(mask & EXPORT_INPUT)
    {
        ini_putl("INPUT", "MouseYawSensitivity", config.sensX, iniPath);
        ini_putl("INPUT", "MousePitchSensitivity", config.sensY, iniPath);
        ini_putf("INPUT", "MouseSensitivityMultiplierUnit", config.multiplier, iniPath);

        ini_putl("INPUT", "ADSMouseUseSpecific", config.customADS, iniPath);
        ini_putl("INPUT", "ADSMouseSensitivityGlobal", config.adsGlobal, iniPath);

        if(config.customADS)
        {
            ini_putl("INPUT", "ADSMouseSensitivity1x", config.ads1x, iniPath);
            ini_putl("INPUT", "ADSMouseSensitivity2xHalf", config.ads2xHalf, iniPath);
            ini_putl("INPUT", "ADSMouseSensitivity3x", config.ads3x, iniPath);
            ini_putl("INPUT", "ADSMouseSensitivity4x", config.ads4x, iniPath);
            ini_putl("INPUT", "ADSMouseSensitivity5x", config.ads5x, iniPath);
            ini_putl("INPUT", "ADSMouseSensitivity12x", config.ads12x, iniPath);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* tiny JSON helpers for webview bindings                              */
/* ------------------------------------------------------------------ */

static void json_escape(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    for(size_t i = 0; in[i] != '\0' && j + 2 < out_size; i++)
    {
        unsigned char c = (unsigned char)in[i];
        if(c == '"' || c == '\\')
        {
            out[j++] = '\\';
            out[j++] = (char)c;
        }
        else if(c == '\n')
        {
            out[j++] = '\\';
            out[j++] = 'n';
        }
        else
        {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

/* Copies a single JSON element's text (already trimmed of surrounding
 * whitespace) into `out`, stripping quotes and unescaping \" \\ \n \t
 * for strings, or copying raw text for numbers/literals. */
static void json_copy_element(const char *start, const char *end, char *out, size_t out_size)
{
    while(start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) start++;
    while(end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;

    size_t j = 0;

    if(start < end && *start == '"' && end[-1] == '"')
    {
        start++;
        end--;
        while(start < end && j + 1 < out_size)
        {
            if(*start == '\\' && start + 1 < end)
            {
                start++;
                switch(*start)
                {
                    case 'n':  out[j++] = '\n'; break;
                    case 't':  out[j++] = '\t'; break;
                    case '"':  out[j++] = '"';  break;
                    case '\\': out[j++] = '\\'; break;
                    default:   out[j++] = *start; break;
                }
                start++;
            }
            else
            {
                out[j++] = *start++;
            }
        }
    }
    else
    {
        while(start < end && j + 1 < out_size) out[j++] = *start++;
    }

    out[j] = '\0';
}

/* Extracts the `index`-th top-level element from a JSON array string
 * such as `[0,"abcd"]`. Returns 0 on success, -1 if not found. */
static int json_arg(const char *req, int index, char *out, size_t out_size)
{
    const char *p = req;
    while(*p && *p != '[') p++;
    if(*p != '[') return -1;
    p++;

    int cur = 0;
    int depth = 0;
    int in_str = 0;
    const char *start = p;

    while(*p)
    {
        char c = *p;

        if(in_str)
        {
            if(c == '\\' && p[1] != '\0') { p += 2; continue; }
            if(c == '"') in_str = 0;
            p++;
            continue;
        }

        if(c == '"') { in_str = 1; p++; continue; }

        if(c == '[' || c == '{') { depth++; p++; continue; }

        if(c == ']' || c == '}')
        {
            if(depth == 0)
            {
                if(cur == index)
                {
                    json_copy_element(start, p, out, out_size);
                    return 0;
                }
                return -1;
            }
            depth--;
            p++;
            continue;
        }

        if(c == ',' && depth == 0)
        {
            if(cur == index)
            {
                json_copy_element(start, p, out, out_size);
                return 0;
            }
            cur++;
            p++;
            start = p;
            continue;
        }

        p++;
    }

    return -1;
}

static int json_arg_int(const char *req, int index, int default_val)
{
    char buf[32];
    if(json_arg(req, index, buf, sizeof(buf)) != 0) return default_val;
    return atoi(buf);
}

/* ------------------------------------------------------------------ */
/* webview bindings                                                    */
/* ------------------------------------------------------------------ */

static void js_get_profiles(const char *seq, const char *req, void *arg)
{
    webview_t w = (webview_t)arg;
    (void)req;

    char json[4096];
    size_t pos = 0;

    pos += (size_t)snprintf(json + pos, sizeof(json) - pos, "[");
    for(int i = 0; i < g_profileCount; i++)
    {
        char esc[256];
        json_escape(g_profiles[i], esc, sizeof(esc));
        pos += (size_t)snprintf(json + pos, sizeof(json) - pos, "%s\"%s\"", i ? "," : "", esc);
    }
    snprintf(json + pos, sizeof(json) - pos, "]");

    webview_return(w, seq, 0, json);
}

static void js_export_config(const char *seq, const char *req, void *arg)
{
    webview_t w = (webview_t)arg;

    int idx = json_arg_int(req, 0, -1);
    if(idx < 0 || idx >= g_profileCount)
    {
        webview_return(w, seq, 0, "{\"ok\":false,\"error\":\"Invalid profile.\"}");
        return;
    }

    int mask = json_arg_int(req, 1, EXPORT_ALL);

    char iniPath[768];
    get_settings_path(idx, iniPath, sizeof(iniPath));

    char *encoded = do_export(iniPath, (uint8_t)mask);
    if(!encoded)
    {
        webview_return(w, seq, 0, "{\"ok\":false,\"error\":\"Failed to export config.\"}");
        return;
    }

    char result[8192];
    snprintf(result, sizeof(result), "{\"ok\":true,\"code\":\"%s\"}", encoded);
    free(encoded);

    webview_return(w, seq, 0, result);
}

static void js_import_config(const char *seq, const char *req, void *arg)
{
    webview_t w = (webview_t)arg;

    int idx = json_arg_int(req, 0, -1);

    char config_str[4096];
    json_arg(req, 1, config_str, sizeof(config_str));

    if(idx < 0 || idx >= g_profileCount)
    {
        webview_return(w, seq, 0, "{\"ok\":false,\"error\":\"Invalid profile.\"}");
        return;
    }

    char iniPath[768];
    get_settings_path(idx, iniPath, sizeof(iniPath));

    int result = do_import(iniPath, config_str);

    const char *err = NULL;
    switch(result)
    {
        case 0: break;
        case 1: err = "Failed to decode config."; break;
        case 2: err = "Failed to decompress config."; break;
        case 3: err = "Decompressed size doesn't match expected payload size."; break;
        case 4: err = "Config doesn't contain header magic."; break;
        case 5: err = "Config version doesn't match app version."; break;
        case 6: err = "Config is corrupted."; break;
        case 7: err = "Invalid config."; break;
        default: err = "Unknown error."; break;
    }

    if(err)
    {
        char escaped[256];
        json_escape(err, escaped, sizeof(escaped));

        char buf[512];
        snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", escaped);
        webview_return(w, seq, 0, buf);
    }
    else
    {
        webview_return(w, seq, 0, "{\"ok\":true}");
    }
}

/* ------------------------------------------------------------------ */
/* entry point                                                         */
/* ------------------------------------------------------------------ */

int main(void)
{
    load_profiles();

    webview_t w = webview_create(0, NULL);

    webview_set_title(w, "R6 Config | @gottvonterrorismus");
    webview_set_size(w, 430, 232, WEBVIEW_HINT_FIXED);

    webview_bind(w, "getProfiles", js_get_profiles, w);
    webview_bind(w, "exportConfig", js_export_config, w);
    webview_bind(w, "importConfig", js_import_config, w);

    webview_set_html(w, UI_HTML);

    webview_run(w);
    webview_destroy(w);

    return 0;
}
