#include "pch.h"
#include <windows.h>
#include <winhttp.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <random>
#include <fstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "chaomod_msg.h"
#include "json.hpp"
#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

struct ChaoItem {
    uint16_t type;
    uint16_t garden;
    uint32_t unknown;
    float x, y, z;
};
static const int CHAO_SLOTS = 10;

// ---- item names, indexed by item type id ----
static const char* NAMES[25] = {
    "penguin", "seal", "otter", "rabbit", "cheetah", "boar", "bear", "tiger", "gorilla", "peacock",
    "parrot", "condor", "skunk", "sheep", "raccoon", "half_fish", "skeleton_dog", "bat", "dragon",
    "unicorn", "phoenix", "yellow_drive", "green_drive", "red_drive", "purple_drive" };

// ---- reward rules: same numbers as rewards.json in the Python version ----
static const int    MIN_ROUNDS = 10;
static const size_t MAX_ITEMS = 10;
static const size_t MAX_MATCHES_PER_RUN = 3;
struct DriveRule { int type; const char* stat; int per; };
static const DriveRule DRIVES[] = {
    { 23, "total_kills", 5 },{ 24, "total_hs_kills", 4 },
    { 22, "trade_kills_succeed", 1 },{ 21, "flashbang_hit_foe", 3 } };
struct AnimalStat { const char* stat; int per; };
static const AnimalStat ANIMAL_STATS[] = {
    { "multi3k", 1 },{ "multi4k", 1 },{ "multi5k", 1 },
    { "flashbang_leading_to_kill", 1 },{ "flash_assist", 1 } };
static const int ANIMAL_POOL[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
static const int RARE_POOL[] = { 19, 18, 20, 17, 15, 16 };
static const double RARE_AT_LEAST = 0.07;

static uint32_t Fnv1a(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

static double Stat(const json& st, const char* name)
{
    auto it = st.find(name);
    return (it != st.end() && it->is_number()) ? it->get<double>() : 0.0;
}

static std::vector<int> RewardsFor(const std::string& matchId, const json& st, int& rounds)
{
    rounds = (int)Stat(st, "rounds_count");
    std::vector<int> out;
    if (rounds < MIN_ROUNDS) return out;

    std::vector<int> drives, animals, rare;
    for (const auto& d : DRIVES) {
        int n = (int)(Stat(st, d.stat) / d.per);
        for (int i = 0; i < n; i++) drives.push_back(d.type);
    }
    int k = 0;
    for (const auto& a : ANIMAL_STATS) k += (int)(Stat(st, a.stat) / a.per);
    if (k > 0) {
        std::mt19937 rng(Fnv1a(matchId));
        for (int i = 0; i < k; i++) animals.push_back(ANIMAL_POOL[rng() % (sizeof(ANIMAL_POOL) / sizeof(int))]);
    }
    if (Stat(st, "leetify_rating") >= RARE_AT_LEAST) {
        std::mt19937 rng(Fnv1a(matchId + "rare"));
        rare.push_back(RARE_POOL[rng() % (sizeof(RARE_POOL) / sizeof(int))]);
    }
    out = rare;
    out.insert(out.end(), animals.begin(), animals.end());
    out.insert(out.end(), drives.begin(), drives.end());
    if (out.size() > MAX_ITEMS) out.resize(MAX_ITEMS);
    return out;
}

static std::string Describe(const std::vector<int>& items)
{
    int cnt[25] = { 0 };
    for (int t : items) if (t >= 0 && t < 25) cnt[t]++;
    std::string s;
    for (int t = 0; t < 25; t++) {
        if (!cnt[t]) continue;
        if (!s.empty()) s += ", ";
        s += std::string(NAMES[t]) + " x" + std::to_string(cnt[t]);
    }
    return s.empty() ? "nothing" : s;
}

// ---- settings and state files (both live in the mod's own folder) ----
static std::string g_modDir;
static std::string g_steam;
static int g_firstRunMatches = 1;
static bool g_msgShown = false;      // entry message already shown this launch
static int g_deliveredCount = 0;     // items written this launch
// ---- log file (safe to use from any thread) and Say(): console + log in one call ----
static CRITICAL_SECTION g_logLock;
static bool g_logReady = false;

static void Log(const char* fmt, ...)
{
    if (!g_logReady) return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    std::string logPath = g_modDir + "\\chaomod.log";
    SYSTEMTIME t;
    GetLocalTime(&t);
    EnterCriticalSection(&g_logLock);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExA(logPath.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 262144)
        MoveFileExA(logPath.c_str(), (logPath + ".old").c_str(), MOVEFILE_REPLACE_EXISTING);
    FILE* f = nullptr;
    if (fopen_s(&f, logPath.c_str(), "a") == 0 && f) {
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, msg);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

static void Say(const char* fmt, ...)
{
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    PrintDebug("%s", msg);                       // game console
    size_t n = strlen(msg);
    while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = 0;
    Log("%s", msg);                              // chaomod.log
}
// ---- settings: config.ini (written by the Mod Manager's Configure window), with settings.json as a fallback ----
static bool g_keepLog = true;
static std::map<std::string, std::string> g_ini;   // "section|key" in lower case -> value

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static void ReadIni(const std::string& file)
{
    g_ini.clear();
    std::ifstream f(file);
    if (!f) return;
    std::string line, section;
    bool first = true;
    while (std::getline(f, line)) {
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            line.erase(0, 3);                           // skip a UTF-8 byte-order mark
        first = false;
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') { section = Lower(Trim(line.substr(1, line.size() - 2))); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        g_ini[section + "|" + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
    }
}

static bool IniGet(const char* key, std::string& out)
{
    std::string k = Lower(key);
    auto it = g_ini.find("general|" + k);               // the group name in configschema.xml
    if (it == g_ini.end()) it = g_ini.find("|" + k);    // or a key outside any section
    if (it == g_ini.end()) return false;
    out = it->second;
    return true;
}

static bool ValidSteam(const std::string& s)
{
    return s.size() == 17 && s.find_first_not_of("0123456789") == std::string::npos;
}

static bool LoadSettings(std::string& err)
{
    const char* source = "";
    ReadIni(g_modDir + "\\config.ini");
    std::string v;
    if (IniGet("SteamID64", v)) g_steam = Trim(v);
    if (IniGet("FirstRunMatches", v)) g_firstRunMatches = atoi(v.c_str());
    if (IniGet("KeepLog", v)) g_keepLog = (Lower(v) == "true" || v == "1");
    if (ValidSteam(g_steam)) source = "config.ini";

    if (!ValidSteam(g_steam)) {                         // fall back to our own testing file
        std::string p = g_modDir + "\\settings.json";
        if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            try {
                std::ifstream f(p);
                json j = json::parse(f);
                if (j.contains("steam64_id") && j["steam64_id"].is_number_unsigned())
                    g_steam = std::to_string(j["steam64_id"].get<unsigned long long>());
                else
                    g_steam = j.value("steam64_id", std::string());
                g_firstRunMatches = j.value("first_run_matches", g_firstRunMatches);
                if (ValidSteam(g_steam)) source = "settings.json";
            } catch (const std::exception&) {}
        }
    }

    Log("settings loaded from %s", ValidSteam(g_steam) ? source : "nowhere (no valid SteamID64 found)");
    g_logReady = g_keepLog;                             // KeepLog switched off: no more log lines from here on
    if (!ValidSteam(g_steam)) {
        err = "Setup needed: open the SA Mod Manager, select chaomod, click Configure and enter your SteamID64 (17 digits)";
        return false;
    }
    return true;
}

struct State {
    bool exists = false;
    std::vector<std::string> processed;   // newest first
    std::vector<int> pending;             // item type ids waiting for delivery
};

static bool LoadState(State& s, std::string& err)
{
    std::string p = g_modDir + "\\state.json";
    if (GetFileAttributesA(p.c_str()) == INVALID_FILE_ATTRIBUTES) return true;   // no file = first run
    std::ifstream f(p);
    if (!f) { err = "state.json exists but cannot be opened (left untouched)"; return false; }
    try {
        json j = json::parse(f);
        for (const auto& id : j.at("processed")) s.processed.push_back(id.get<std::string>());
        for (const auto& t : j.at("pending")) s.pending.push_back(t.get<int>());
        s.exists = true;
        return true;
    }
    catch (const std::exception& e) {
        err = std::string("state.json unreadable (left untouched): ") + e.what();
        return false;
    }
}

static bool SaveState(const State& s)
{
    json j;
    j["version"] = 1;
    j["processed"] = s.processed;
    j["pending"] = s.pending;
    std::string fin = g_modDir + "\\state.json";
    std::string tmp = fin + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        if (!o) return false;
        o << j.dump(2);
        if (!o) return false;
    }
    return MoveFileExA(tmp.c_str(), fin.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// ---- network + parsing + state: runs on its own thread. It must NOT touch game memory or call PrintDebug. ----
static volatile LONG  g_netState = 0;   // 0 = not started, 1 = running, 2 = finished
static volatile DWORD g_netStatus = 0;
static volatile DWORD g_netBytes = 0;
static volatile DWORD g_netError = 0;
static char g_summary[300] = "";         // written before g_netState becomes 2
static std::string g_report;             // same
static State g_state;                    // the live copy of state.json, used by the game thread for delivery
static volatile LONG g_stateReady = 0;   // 1 once g_state is trustworthy and saved

static DWORD WINAPI NetThread(LPVOID)
{
    InterlockedExchange(&g_netState, 1);

    State state;
    std::string err;
    if (!LoadState(state, err)) {
        snprintf(g_summary, sizeof(g_summary), "%s", err.c_str());
        InterlockedExchange(&g_netState, 2);
        return 0;
    }

    std::wstring path = L"/v3/profile/matches?steam64_id=";
    path += std::wstring(g_steam.begin(), g_steam.end());

    HINTERNET s = WinHttpOpen(L"chaomod/0.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET c = s ? WinHttpConnect(s, L"api-public.cs-prod.leetify.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    if (s) WinHttpSetTimeouts(s, 10000, 10000, 10000, 20000);

    bool ok = r != NULL
        && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(r, NULL);

    std::string body;
    if (ok) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        g_netStatus = status;
        char buf[8192];
        DWORD got = 0;
        while (WinHttpReadData(r, buf, sizeof(buf), &got) && got > 0) body.append(buf, got);
        g_netBytes = (DWORD)body.size();
    }
    else {
        g_netError = GetLastError();
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);

    if (g_netStatus == 200) {
        try {
            json j = json::parse(body);
            int total = (int)j.size();
            if (total > 0) {
                // 1. which matches are new?  (newest first)
                std::vector<const json*> fresh;
                if (!state.exists) {
                    int n = g_firstRunMatches < 1 ? 1 : g_firstRunMatches;
                    for (int i = 0; i < n && i < total; i++) fresh.push_back(&j[i]);
                }
                else {
                    std::set<std::string> seen(state.processed.begin(), state.processed.end());
                    for (const auto& m : j) {
                        if (!seen.count(m.value("id", std::string()))) fresh.push_back(&m);
                        if (fresh.size() >= MAX_MATCHES_PER_RUN) break;
                    }
                }

                // 2. work out rewards, oldest match first
                std::vector<int> pending = state.pending;
                std::string report;
                for (auto it = fresh.rbegin(); it != fresh.rend(); ++it) {
                    const json& m = **it;
                    const json* my = nullptr;
                    if (m.contains("stats"))
                        for (const auto& st : m["stats"])
                            if (st.value("steam64_id", std::string()) == g_steam) { my = &st; break; }
                    std::string id = m.value("id", std::string());
                    if (!my) { report += "  new " + id.substr(0, 8) + ": no stats for you, skipped\n"; continue; }
                    int rounds = 0;
                    std::vector<int> items = RewardsFor(id, *my, rounds);
                    pending.insert(pending.end(), items.begin(), items.end());
                    char line[300];
                    snprintf(line, sizeof(line), "  new %.8s rounds=%d -> %d item(s): %s\n", id.c_str(), rounds,
                        (int)items.size(), rounds < MIN_ROUNDS ? "skipped (too few rounds)" : Describe(items).c_str());
                    report += line;
                }

                // 3. everything fetched is now handled; remember it, then save
                State ns;
                ns.exists = true;
                ns.pending = pending;
                std::set<std::string> have;
                for (const auto& m : j) { std::string id = m.value("id", std::string()); if (have.insert(id).second) ns.processed.push_back(id); }
                for (const auto& id : state.processed) if (have.insert(id).second) ns.processed.push_back(id);
                if (ns.processed.size() > 1000) ns.processed.resize(1000);
                bool saved = SaveState(ns);
                if (saved) { g_state = ns; InterlockedExchange(&g_stateReady, 1); }

                report += "  waiting for delivery: " + Describe(pending) + "\n";
                snprintf(g_summary, sizeof(g_summary), "matches=%d first_run=%d new_matches=%d pending=%d state_saved=%d",
                    total, state.exists ? 0 : 1, (int)fresh.size(), (int)pending.size(), saved ? 1 : 0);
                g_report = report;
            }
            else {
                snprintf(g_summary, sizeof(g_summary), "leetify returned no matches");
            }
        }
        catch (const std::exception& e) {
            snprintf(g_summary, sizeof(g_summary), "parse error: %.200s", e.what());
        }
    }
    InterlockedExchange(&g_netState, 2);
    return 0;
}

extern "C"
{
    __declspec(dllexport) void __cdecl Init(const char* path, const HelperFunctions& helperFunctions)
    {
        Say("chaomod: hello (spike 4d: settings + state)\n");
        g_modDir = path;
        while (!g_modDir.empty() && (g_modDir.back() == '\\' || g_modDir.back() == '/')) g_modDir.pop_back();
        InitializeCriticalSection(&g_logLock);
        g_logReady = true;
        Say("chaomod: ---- started ----\n");
        MsgInit(helperFunctions);

        std::string err;
        if (!LoadSettings(err)) {
            snprintf(g_summary, sizeof(g_summary), "%s", err.c_str());
            InterlockedExchange(&g_netState, 2);
            return;
        }
        HANDLE h = CreateThread(NULL, 0, NetThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }

    __declspec(dllexport) void __cdecl OnFrame()
    {
        MsgFrame();
        static bool netReported = false;
        if (!netReported && g_netState == 2) {
            Say("chaomod: leetify status=%lu bytes=%lu error=%lu %s\n%s",
                (unsigned long)g_netStatus, (unsigned long)g_netBytes,
                (unsigned long)g_netError, g_summary, g_report.c_str());
            netReported = true;
        }

        static int tick = 0;
        static char last[256] = "";
        if (++tick % 30 != 0) return;

        ChaoItem* items = (ChaoItem*)ChaoAnimalSlots;
        int freeSlots = 0;
        int ok = 1;
        char types[CHAO_SLOTS * 5 + 1] = "";
        for (int i = 0; i < CHAO_SLOTS; i++) {
            sprintf_s(types + i * 5, 6, "%04X ", items[i].type);
            if (items[i].type == 0xFFFF && items[i].garden == 0xFFFF) { freeSlots++; continue; }
            if (items[i].type > 24 || items[i].garden < 1 || items[i].garden > 3) ok = 0;
        }
        bool inLobby = (int)CurrentLevel == 90 && (int)CurrentChaoArea == 7
            && (int)GameState == 16 && ok == 1;
        if (inLobby && g_netState == 2 && g_stateReady && !g_state.pending.empty() && freeSlots > 0) {
            int written = 0;
            for (int i = 0; i < CHAO_SLOTS && !g_state.pending.empty(); i++) {
                if (!(items[i].type == 0xFFFF && items[i].garden == 0xFFFF)) continue;
                int type = g_state.pending.front();
                if (type < 0 || type > 24) { g_state.pending.erase(g_state.pending.begin()); i--; continue; }
                items[i].type = (uint16_t)type;
                items[i].garden = 1;
                items[i].unknown = 0;
                items[i].x = 11.0f + (written % 4) * 2.5f;
                items[i].y = 6.2f;                       // ground height we saw in the saved file
                items[i].z = -100.0f + (written / 4) * 2.5f;
                g_state.pending.erase(g_state.pending.begin());
                written++;
                Say("chaomod: delivered %s into slot %d\n", NAMES[type], i);
            }
            if (written) {
                g_deliveredCount += written;
                bool saved = SaveState(g_state);
                Say("chaomod: delivered %d item(s), %d still waiting, state_saved=%d\n",
                    written, (int)g_state.pending.size(), saved ? 1 : 0);
            }
        }
        if (inLobby && !g_msgShown && g_netState == 2) {
            g_msgShown = true;
            int waiting = (int)g_state.pending.size();
            char head[96];
            if (!g_stateReady && g_netStatus == 0 && g_netError == 0) {
                MsgShow(0xFFFF7B72, "chaomod: setup needed", "Your SteamID64 isn't set up.",
                    "Open the SA Mod Manager, select this mod, click Configure and enter your SteamID64.");
            }
            else if (!g_stateReady && g_netStatus == 0) {
                MsgShow(0xFFFF7B72, "chaomod: can't reach Leetify", "No connection to Leetify.",
                    "Check your internet and restart the game. Details are in chaomod.log.");
            }
            else if (!g_stateReady && g_netStatus != 200) {
                MsgShow(0xFFFF7B72, "chaomod: no matches found", "Leetify didn't return your matches.",
                    "Check your SteamID64 in Configure and make sure your Leetify profile is public.");
            }
            else if (!g_stateReady) {
                MsgShow(0xFFFF7B72, "chaomod: something went wrong", "chaomod couldn't save its state.",
                    "Nothing was delivered. Details are in chaomod.log.");
            }
            else if (g_deliveredCount > 0) {
                snprintf(head, sizeof(head), "%d item%s delivered.", g_deliveredCount, g_deliveredCount == 1 ? "" : "s");
                if (waiting > 0) {
                    char more[96];
                    snprintf(more, sizeof(more), "%d more waiting. Free some space and come back to the lobby.", waiting);
                    MsgShow(0xFF5EE6B0, "chaomod", head, more);
                }
                else {
                    MsgShow(0xFF5EE6B0, "chaomod", head, "Walk into the garden to find them.");
                }
            }
            else if (waiting > 0) {
                snprintf(head, sizeof(head), "%d item%s waiting.", waiting, waiting == 1 ? "" : "s");
                MsgShow(0xFFFFC857, "chaomod", head, "Your garden is full. Use or feed some items, then come back to the lobby.");
            }
            else {
                MsgShow(0xFF6CB6FF, "chaomod", "No new matches.", "Play a CS2 match, let Leetify pick it up, then restart the game.");
            }
        }

        char line[256];
        sprintf_s(line, "slots_ok=%d free=%d GameState=%d Level=%d Area(cur/last/next)=%d/%d/%d types=%s",
            ok, freeSlots, (int)GameState, (int)CurrentLevel,
            (int)CurrentChaoArea, (int)LastChaoArea, (int)NextChaoArea, types);
        if (strcmp(line, last) != 0) {
            Say("chaomod: %s\n", line);
            strcpy_s(last, line);
        }
    }

    __declspec(dllexport) void __cdecl OnInput() {}
    __declspec(dllexport) void __cdecl OnControl() {}
    __declspec(dllexport) ModInfo SA2ModInfo = { ModLoaderVer };
}