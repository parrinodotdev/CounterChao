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
#include <ctime>
#include <stdexcept>
#include "CounterChao_msg.h"
#include "json.hpp"
#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;
static const char* VERSION = "0.4.0-dev";

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

// ---- ring rules: rings for the Black Market, from the same matches that give items ----
static const int RINGS_PER_ROUND_WON = 5;
static const int RINGS_PER_MATCH_WIN = 50;
static const int RINGS_CAP = 9999999;   // never push the game's ring total past this (a safe guess, not a known game limit)

// ---- Leetify: don't ask again if the last good answer is this recent. 0 = always ask. ----
static const long long FETCH_MIN_SECONDS = 180;

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

// A whole-number stat, kept inside a sane range so a nonsense value from Leetify can't cause trouble.
static int StatInt(const json& st, const char* name)
{
    double v = Stat(st, name);
    if (!(v > 0.0)) return 0;
    if (v > 100000.0) return 100000;
    return (int)v;
}

static std::vector<int> RewardsFor(const std::string& matchId, const json& st, int& rounds)
{
    rounds = StatInt(st, "rounds_count");
    std::vector<int> out;
    if (rounds < MIN_ROUNDS) return out;

    std::vector<int> drives, animals, rare;
    for (const auto& d : DRIVES) {
        int n = StatInt(st, d.stat) / d.per;
        if (n > (int)MAX_ITEMS) n = (int)MAX_ITEMS;   // a match never gives more than MAX_ITEMS anyway
        for (int i = 0; i < n; i++) drives.push_back(d.type);
    }
    int k = 0;
    for (const auto& a : ANIMAL_STATS) k += StatInt(st, a.stat) / a.per;
    if (k > (int)MAX_ITEMS) k = (int)MAX_ITEMS;
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

// Rings for one match. Same minimum length as items. A win is more rounds won than lost.
static int RingsFor(const json& st)
{
    if (StatInt(st, "rounds_count") < MIN_ROUNDS) return 0;
    int won = StatInt(st, "rounds_won");
    int lost = StatInt(st, "rounds_lost");
    return won * RINGS_PER_ROUND_WON + (won > lost ? RINGS_PER_MATCH_WIN : 0);
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
static int g_deliveredRings = 0;     // rings added this launch
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

    std::string logPath = g_modDir + "\\CounterChao.log";
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
    Log("%s", msg);                              // CounterChao.log
}
// ---- settings: config.ini (written by the Mod Manager's Configure window), with settings.json as a fallback ----
static bool g_keepLog = true;
static bool g_closeAfterSave = false;   // quick exit, off unless ticked in Configure
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
    if (IniGet("CloseAfterSave", v)) g_closeAfterSave = (Lower(v) == "true" || v == "1");
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

    if (g_firstRunMatches < 1) g_firstRunMatches = 1;   // same limits as the Configure window
    if (g_firstRunMatches > 5) g_firstRunMatches = 5;

    Log("settings loaded from %s", ValidSteam(g_steam) ? source : "nowhere (no valid SteamID64 found)");
    Log("quick exit (close the game after saving): %s", g_closeAfterSave ? "on" : "off");
    g_logReady = g_keepLog;                             // KeepLog switched off: no more log lines from here on
    if (!ValidSteam(g_steam)) {
        err = "Setup needed: open the SA Mod Manager, select CounterChao, click Configure and enter your SteamID64 (17 digits)";
        return false;
    }
    return true;
}

struct State {
    bool exists = false;
    std::vector<std::string> processed;   // newest first
    std::vector<int> pending;             // item type ids waiting for delivery
    int pendingRings = 0;                 // rings waiting for delivery
    long long lastFetch = 0;              // when Leetify last gave a good answer (seconds since 1970, UTC). 0 = never
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
        auto pr = j.find("pending_rings");   // missing in state files from before rings existed
        if (pr != j.end() && pr->is_number_integer()) s.pendingRings = pr->get<int>();
        if (s.pendingRings < 0 || s.pendingRings > RINGS_CAP) s.pendingRings = 0;
        auto lf = j.find("last_fetch");      // missing in state files from before 0.3.0
        if (lf != j.end() && lf->is_number_integer()) s.lastFetch = lf->get<long long>();
        if (s.lastFetch < 0) s.lastFetch = 0;
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
    j["pending_rings"] = s.pendingRings;
    j["last_fetch"] = s.lastFetch;
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

// what happened this launch; decides which lobby message is shown
enum Outcome : LONG {
    OUT_NONE = 0,
    OUT_OK,           // Leetify answered, state saved
    OUT_SETUP,        // no valid SteamID64
    OUT_STATE_BAD,    // state.json exists but can't be read
    OUT_NET_FAIL,     // no HTTP response at all
    OUT_HTTP_ERR,     // HTTP response other than 200
    OUT_NO_MATCHES,   // 200 with an empty match list
    OUT_PARSE,        // 200 but the reply wasn't the JSON we expect
    OUT_SAVE_FAIL,    // couldn't write state.json
    OUT_SKIPPED       // Leetify was asked a moment ago, so this launch didn't ask again
};
static volatile LONG g_outcome = OUT_NONE;   // written before g_netState becomes 2

static DWORD WINAPI NetThread(LPVOID)
{
    InterlockedExchange(&g_netState, 1);

    State state;
    std::string err;
    if (!LoadState(state, err)) {
        snprintf(g_summary, sizeof(g_summary), "%s", err.c_str());
        InterlockedExchange(&g_outcome, OUT_STATE_BAD);
        InterlockedExchange(&g_netState, 2);
        return 0;
    }

    // Asked Leetify a moment ago and got a good answer? Don't ask again yet.
    // Anything already waiting is still delivered. A clock that went backwards never blocks a fetch.
    long long nowSec = (long long)time(nullptr);
    if (state.exists && FETCH_MIN_SECONDS > 0 && state.lastFetch > 0
        && nowSec >= state.lastFetch && nowSec - state.lastFetch < FETCH_MIN_SECONDS) {
        g_state = state;
        InterlockedExchange(&g_stateReady, 1);
        snprintf(g_summary, sizeof(g_summary), "fetch skipped: last good fetch was %lld s ago (minimum %lld) pending=%d pending_rings=%d",
            nowSec - state.lastFetch, FETCH_MIN_SECONDS, (int)state.pending.size(), state.pendingRings);
        InterlockedExchange(&g_outcome, OUT_SKIPPED);
        InterlockedExchange(&g_netState, 2);
        return 0;
    }

    std::wstring path = L"/v3/profile/matches?steam64_id=";
    path += std::wstring(g_steam.begin(), g_steam.end());

    std::string uaNarrow = std::string("CounterChao/") + VERSION;   // R12: follows VERSION, e.g. CounterChao/0.4.0-dev
    std::wstring ua(uaNarrow.begin(), uaNarrow.end());
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    // Time limits have to be set before the request is created: a request copies them from the session at that moment.
    // Resolve, connect, send: 10 s each. Receive: 60 s, because Leetify has taken over 30 s to answer.
    if (s) WinHttpSetTimeouts(s, 10000, 10000, 10000, 60000);
    HINTERNET c = s ? WinHttpConnect(s, L"api-public.cs-prod.leetify.com", INTERNET_DEFAULT_HTTPS_PORT, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;

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

    LONG outcome = OUT_NET_FAIL;
    if (g_netStatus == 200) {
        try {
            json j = json::parse(body);
            if (!j.is_array()) throw std::runtime_error("reply is not a list of matches");
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
                int pendingRings = state.pendingRings;
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
                    int rings = RingsFor(*my);
                    bool ringsFit = pendingRings + rings <= RINGS_CAP;
                    if (ringsFit) pendingRings += rings;
                    char line[300];
                    snprintf(line, sizeof(line), "  new %.8s rounds=%d -> %d item(s): %s\n", id.c_str(), rounds,
                        (int)items.size(), rounds < MIN_ROUNDS ? "skipped (too few rounds)" : Describe(items).c_str());
                    report += line;
                    snprintf(line, sizeof(line), "      rounds won=%d lost=%d -> %d ring(s)\n",
                        StatInt(*my, "rounds_won"), StatInt(*my, "rounds_lost"), rings);
                    report += line;
                    if (!ringsFit) report += "      waiting rings are at the limit, these were not added\n";
                }

                // 3. everything fetched is now handled; remember it, then save
                State ns;
                ns.exists = true;
                ns.pending = pending;
                ns.pendingRings = pendingRings;
                ns.lastFetch = nowSec;
                std::set<std::string> have;
                for (const auto& m : j) { std::string id = m.value("id", std::string()); if (have.insert(id).second) ns.processed.push_back(id); }
                for (const auto& id : state.processed) if (have.insert(id).second) ns.processed.push_back(id);
                if (ns.processed.size() > 1000) ns.processed.resize(1000);
                bool saved = SaveState(ns);
                if (saved) { g_state = ns; InterlockedExchange(&g_stateReady, 1); }
                outcome = saved ? OUT_OK : OUT_SAVE_FAIL;

                report += "  waiting for delivery: " + Describe(pending) + ", rings x" + std::to_string(pendingRings) + "\n";
                snprintf(g_summary, sizeof(g_summary), "matches=%d first_run=%d new_matches=%d pending=%d pending_rings=%d state_saved=%d",
                    total, state.exists ? 0 : 1, (int)fresh.size(), (int)pending.size(), pendingRings, saved ? 1 : 0);
                g_report = report;
            }
            else {
                outcome = OUT_NO_MATCHES;
                snprintf(g_summary, sizeof(g_summary), "leetify returned no matches");
            }
        }
        catch (const std::exception& e) {
            outcome = OUT_PARSE;
            snprintf(g_summary, sizeof(g_summary), "parse error: %.200s", e.what());
        }
    }
    else if (g_netStatus != 0) {
        outcome = OUT_HTTP_ERR;
    }

    // Leetify gave us nothing usable, but state.json loaded fine:
    // items that were already waiting can still be delivered.
    if (outcome != OUT_OK && outcome != OUT_SAVE_FAIL && state.exists) {
        g_state = state;
        InterlockedExchange(&g_stateReady, 1);
    }
    InterlockedExchange(&g_outcome, outcome);
    InterlockedExchange(&g_netState, 2);
    return 0;
}

// ---- quick exit: once the Chao World exit teleporter has saved, close the game. Off unless ticked in Configure. ----
// It never closes the game unless it has seen a fresh Chao save file on disk, and no save file has been written for 3 seconds.
// It only closes the game at stage select (Level 0, GameState 0). After a Chao Key visit the game goes on to the
// next story stage instead; then quick exit stands down.
// Exit game in the garden pause menu (R15) saves with GameState 29 and goes to the title screen or stage select, but
// CurrentLevel stays 90 until a stage loads. So that path is followed separately: QX_EXIT_GAME waits for the save
// to finish and closes with a short message instead of the 3-second countdown.
enum { QX_IDLE, QX_IN_CHAO, QX_WAIT_SAVE, QX_EXIT_GAME, QX_CLOSING, QX_DONE };
static int g_qx = QX_IDLE;
static bool g_qxFromExitGame = false;      // the current close was started by Exit game
static volatile bool g_exitGameSeen = false;   // set by OnFrame (every frame) when GameState 29 shows in Chao World
static ULONGLONG g_qxLeftTick = 0, g_qxCloseTick = 0;   // GetTickCount64 values
static ULONGLONG g_qxLeftTime = 0, g_qxLastNewest = 0, g_qxLastAlf = 0;    // file times

static ULONGLONG FtToU64(const FILETIME& ft) { return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime; }

// Last-write times of the save files: the Chao save (SONIC2B__ALF) and the newest of all SONIC2B__* files.
static bool SaveTimes(ULONGLONG& alf, ULONGLONG& newest)
{
    alf = 0; newest = 0;
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::string dir(exe);
    size_t slash = dir.find_last_of("\\/");
    if (slash == std::string::npos) return false;
    dir = dir.substr(0, slash) + "\\resource\\gd_PC\\SAVEDATA\\";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "SONIC2B__*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExA((dir + fd.cFileName).c_str(), GetFileExInfoStandard, &fa)) continue;
        ULONGLONG t = FtToU64(fa.ftLastWriteTime);
        if (t > newest) newest = t;
        if (_stricmp(fd.cFileName, "SONIC2B__ALF") == 0) alf = t;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return alf != 0;
}

static BOOL CALLBACK QxFindWindow(HWND w, LPARAM out)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    *(HWND*)out = w;
    return FALSE;
}

// Called about twice a second from OnFrame.
static void QuickExitTick()
{
    if (!g_closeAfterSave || g_qx == QX_DONE) return;
    int level = (int)CurrentLevel, gs = (int)GameState;
    bool inChao = level == 90;
    ULONGLONG now = GetTickCount64();
    ULONGLONG alf = 0, newest = 0;
    bool ok = SaveTimes(alf, newest);
    if (ok && g_qxLastAlf != 0 && alf != g_qxLastAlf)      // for the log: when exactly does the game save?
        Say("CounterChao: quick exit: Chao save file written (GameState=%d Level=%d)\n", (int)GameState, (int)CurrentLevel);
    if (ok) g_qxLastAlf = alf;

    bool exitGame = g_exitGameSeen;
    g_exitGameSeen = false;

    if (g_qx == QX_CLOSING) {                               // a save file changed during the countdown: wait again
        if (ok && newest != g_qxLastNewest) {
            g_qx = g_qxFromExitGame ? QX_EXIT_GAME : QX_WAIT_SAVE;
            g_qxLeftTick = now;
            Say("CounterChao: quick exit: a save file was written again, waiting\n");
        }
        return;
    }

    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    ULONGLONG nowTime = FtToU64(nowFt);
    const ULONGLONG WINDOW = 30ULL * 10000000ULL;           // 30 seconds, in file-time units
    const ULONGLONG QUIET = 3ULL * 10000000ULL;             // 3 seconds since the newest save file was written

    // GameState 16 = actually playing. Story mode also passes through Level 90 between stages without entering Chao World.
    if (g_qx == QX_IDLE) { if (inChao && gs == 16) g_qx = QX_IN_CHAO; return; }
    if (g_qx == QX_IN_CHAO) {
        if (exitGame) {                                     // Exit game from the garden pause menu
            g_qxLeftTime = nowTime;
            g_qxLeftTick = now;
            g_qxLastNewest = 0;
            g_qx = QX_EXIT_GAME;
            Say("CounterChao: quick exit: Exit game chosen, waiting for the save to finish\n");
            return;
        }
        if (inChao) return;
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        g_qxLeftTime = FtToU64(ft);
        g_qxLeftTick = now;
        g_qxLastNewest = 0;
        g_qx = QX_WAIT_SAVE;
        Say("CounterChao: quick exit: left Chao World, checking that the game saved\n");
        return;
    }
    bool savedRecently = ok && alf + WINDOW >= g_qxLeftTime;
    bool quiet = nowTime >= newest && nowTime - newest >= QUIET;

    if (g_qx == QX_EXIT_GAME) {
        // After Exit game the game sits at the title screen or stage select with GameState 0 (Level still reads 90).
        // Anything else means the player moved on: leave the game open.
        if (gs != 0 && gs != 29) {
            Say("CounterChao: quick exit: the game moved on after Exit game (GameState=%d Level=%d), leaving the game open\n", gs, level);
            g_qx = QX_IDLE;
            return;
        }
        if (gs == 0 && savedRecently && quiet) {
            g_qxLastNewest = newest;
            g_qxFromExitGame = true;
            g_qx = QX_CLOSING;
            g_qxCloseTick = now + 1000;                     // a short look at the message, no countdown
            Say("CounterChao: quick exit: Exit game saved, closing the game\n");
        }
        else if (now - g_qxLeftTick > 30000) {
            Say("CounterChao: quick exit: no fresh Chao save found after Exit game, leaving the game open\n");
            g_qx = QX_IDLE;
        }
        return;
    }

    // QX_WAIT_SAVE
    if (inChao) { g_qx = QX_IN_CHAO; return; }              // went straight back in: start over
    if (level != 0) {                                       // the game went on to a stage: not ours to close
        Say("CounterChao: quick exit: the game went on to a stage (Level=%d), leaving the game open\n", level);
        g_qx = QX_IDLE;
        return;
    }
    if (gs == 0 && savedRecently && quiet) {
        g_qxLastNewest = newest;
        g_qxFromExitGame = false;
        g_qx = QX_CLOSING;
        g_qxCloseTick = now + 3000;
        Say("CounterChao: quick exit: save finished, closing the game in 3 seconds\n");
    }
    else if (now - g_qxLeftTick > 30000) {
        Say("CounterChao: quick exit: no fresh Chao save found, leaving the game open\n");
        g_qx = QX_IDLE;
    }
}

// Called every frame from OnFrame.
static void QuickExitFrame()
{
    if (g_qx != QX_CLOSING) return;
    if (g_qxFromExitGame) {
        // Level still reads 90 after Exit game, so only GameState tells us the player is still on a menu.
        if ((int)GameState != 0) {
            Say("CounterChao: quick exit: the game moved on (GameState=%d Level=%d), leaving the game open\n",
                (int)GameState, (int)CurrentLevel);
            g_qx = QX_IDLE;
            return;
        }
    }
    else if ((int)CurrentLevel == 90) { g_qx = QX_IN_CHAO; return; }   // back in Chao World: cancel
    else if ((int)CurrentLevel != 0 || (int)GameState != 0) {          // no longer at stage select: cancel
        Say("CounterChao: quick exit: the game moved on (GameState=%d Level=%d), leaving the game open\n",
            (int)GameState, (int)CurrentLevel);
        g_qx = QX_IDLE;
        return;
    }
    if (g_msg.haveHelpers) {
        float H = VerticalResolution;
        if (!(H >= 200.0f && H <= 10000.0f)) H = 600.0f;
        g_msgHelpers.SetDebugFontSize(MSG_BASE_SIZE * H / 600.0f);
        g_msgHelpers.SetDebugFontColor((int)0xFF5EE6B0);
        g_msgHelpers.DisplayDebugString(NJM_LOCATION(2, 3), "CounterChao: game saved. Closing...");
    }
    if (GetTickCount64() < g_qxCloseTick) return;
    g_qx = QX_DONE;
    HWND w = MainWindowHandle;
    if (!w || !IsWindow(w)) { w = NULL; EnumWindows(QxFindWindow, (LPARAM)&w); }
    Say("CounterChao: quick exit: closing the game now (window %s)\n", w ? "found" : "NOT found");
    if (w) PostMessageA(w, WM_CLOSE, 0, 0);
}

extern "C"
{
    __declspec(dllexport) void __cdecl Init(const char* path, const HelperFunctions& helperFunctions)
    {
        g_modDir = path;
        while (!g_modDir.empty() && (g_modDir.back() == '\\' || g_modDir.back() == '/')) g_modDir.pop_back();
        InitializeCriticalSection(&g_logLock);
        g_logReady = true;
        Say("CounterChao v%s (built %s %s) ---- started ----\n", VERSION, __DATE__, __TIME__);
        MsgInit(helperFunctions);

        std::string err;
        if (!LoadSettings(err)) {
            snprintf(g_summary, sizeof(g_summary), "%s", err.c_str());
            InterlockedExchange(&g_outcome, OUT_SETUP);
            InterlockedExchange(&g_netState, 2);
            return;
        }
        HANDLE h = CreateThread(NULL, 0, NetThread, NULL, 0, NULL);
        if (h) CloseHandle(h);
    }

    __declspec(dllexport) void __cdecl OnFrame()
    {
        MsgFrame();
        QuickExitFrame();
        static bool netReported = false;
        if (!netReported && g_netState == 2) {
            Say("CounterChao: leetify status=%lu bytes=%lu error=%lu outcome=%ld %s\n%s",
                (unsigned long)g_netStatus, (unsigned long)g_netBytes,
                (unsigned long)g_netError, (long)g_outcome, g_summary, g_report.c_str());
            netReported = true;
        }

        // Where did this Chao World visit start? 0 = stage select. Anything else = a stage, which means a Chao Key visit.
        // On a Chao Key visit the game empties the item slots when the garden loads and refills them with the stage's
        // animals, so items placed in the lobby would be lost. Items wait for a visit from stage select. Rings are not affected.
        // Exit game in the garden pause menu (R15) saves with GameState 29, then goes to the title screen or stage select.
        // CurrentLevel stays 90 until a stage loads, so without this the next visit would still count as a Chao Key visit.
        static int levelBeforeChao = 0;
        static bool inExitGameSave = false;
        int lvlNow = (int)CurrentLevel, gsNow = (int)GameState;
        if (lvlNow != 90) levelBeforeChao = lvlNow;
        if (lvlNow == 90 && gsNow == 29) {
            if (!inExitGameSave) {
                inExitGameSave = true;
                g_exitGameSeen = true;
                Say("CounterChao: Exit game save seen (was Level=%d). The next Chao World visit counts as coming from stage select\n",
                    levelBeforeChao);
            }
            levelBeforeChao = 0;
        }
        else inExitGameSave = false;
        bool fromStage = levelBeforeChao != 0;

        // A Chao World visit ends when the game leaves play (GameState 0) or loads a level that isn't Chao World.
        // The per-visit flags reset here, every frame, so a short GameState 0 isn't missed.
        static bool heldLogged = false;      // one "held" log line per visit
        static bool visitMsgShown = false;   // one lobby message per visit
        if (lvlNow != 90 || gsNow == 0) { heldLogged = false; visitMsgShown = false; }

        static int tick = 0;
        static char last[512] = "";
        if (++tick % 30 != 0) return;
        QuickExitTick();

        ChaoItem* items = (ChaoItem*)ChaoAnimalSlots;
        int freeSlots = 0;
        int ok = 1;
        char slots[CHAO_SLOTS * 10 + 1] = "";   // "type/garden" for each slot
        for (int i = 0; i < CHAO_SLOTS; i++) {
            sprintf_s(slots + i * 10, 11, "%04X/%04X ", items[i].type, items[i].garden);
            if (items[i].type == 0xFFFF && items[i].garden == 0xFFFF) { freeSlots++; continue; }
            // An occupied slot is a known type lying in garden 1 to 3, or with garden FFFF:
            // that is how the game holds animals and drives just brought in from a stage.
            uint16_t g = items[i].garden;
            bool gardenOk = (g >= 1 && g <= 3) || g == 0xFFFF;
            if (items[i].type > 24 || !gardenOk) ok = 0;
        }
        bool lobbyArea = (int)CurrentLevel == 90 && (int)CurrentChaoArea == 7 && (int)GameState == 16;
        bool inLobby = lobbyArea && ok == 1;   // delivery needs every slot to look familiar
        // Rings: add what's waiting to the game's ring total. The game writes it to the save at the exit teleporter.
        int ringsNow = 0;
        if (inLobby && g_netState == 2 && g_stateReady && g_state.pendingRings > 0) {
            int have = (int)TotalRings;
            int add = g_state.pendingRings;
            if (have < 0 || have >= RINGS_CAP) add = 0;
            else if (add > RINGS_CAP - have) add = RINGS_CAP - have;
            if (add < g_state.pendingRings)
                Say("CounterChao: %d ring(s) not added, the ring total is at its limit (%d)\n", g_state.pendingRings - add, RINGS_CAP);
            if (add > 0) TotalRings = have + add;
            g_state.pendingRings = 0;
            bool saved = SaveState(g_state);
            ringsNow = add;
            g_deliveredRings += add;
            Say("CounterChao: added %d ring(s), total %d -> %d, state_saved=%d\n", add, have, (int)TotalRings, saved ? 1 : 0);
        }
        if (inLobby && fromStage && g_netState == 2 && g_stateReady && !g_state.pending.empty() && !heldLogged) {
            heldLogged = true;
            Say("CounterChao: %d item(s) held: this visit came from a stage (Level=%d). They are delivered on a visit from stage select\n",
                (int)g_state.pending.size(), levelBeforeChao);
        }
        int written = 0;
        bool dropped = false;
        if (inLobby && !fromStage && g_netState == 2 && g_stateReady && !g_state.pending.empty() && freeSlots > 0) {
            for (int i = 0; i < CHAO_SLOTS && !g_state.pending.empty(); i++) {
                if (!(items[i].type == 0xFFFF && items[i].garden == 0xFFFF)) continue;
                int type = g_state.pending.front();
                if (type < 0 || type > 24) {
                    Say("CounterChao: dropped a waiting item with unknown type %d\n", type);
                    g_state.pending.erase(g_state.pending.begin());
                    dropped = true;
                    i--;
                    continue;
                }
                items[i].type = (uint16_t)type;
                items[i].garden = 1;
                items[i].unknown = 0;
                items[i].x = 11.0f + (written % 4) * 2.5f;
                items[i].y = 6.2f;                       // ground height we saw in the saved file
                items[i].z = -100.0f + (written / 4) * 2.5f;
                g_state.pending.erase(g_state.pending.begin());
                written++;
                Say("CounterChao: delivered %s into slot %d\n", NAMES[type], i);
            }
            if (dropped && !written) SaveState(g_state);
            if (written) {
                g_deliveredCount += written;
                bool saved = SaveState(g_state);
                Say("CounterChao: delivered %d item(s), %d still waiting, state_saved=%d\n",
                    written, (int)g_state.pending.size(), saved ? 1 : 0);
                g_msgShown = true;   // this delivery message replaces the entry message
                visitMsgShown = true;
                {
                    char head[96];
                    int left = (int)g_state.pending.size();
                    if (ringsNow > 0)
                        snprintf(head, sizeof(head), "%d item%s and %d rings delivered.", written, written == 1 ? "" : "s", ringsNow);
                    else
                        snprintf(head, sizeof(head), "%d item%s delivered.", written, written == 1 ? "" : "s");
                    if (left > 0) {
                        char more[96];
                        snprintf(more, sizeof(more), "%d more waiting. Free some space and come back to the lobby.", left);
                        MsgShow(0xFF5EE6B0, "CounterChao", head, more);
                    }
                    else {
                        MsgShow(0xFF5EE6B0, "CounterChao", head, "Walk into the garden to find them.");
                    }
                }
            }
        }
        if (ringsNow > 0 && written == 0) {   // rings arrived without items: say so
            g_msgShown = true;
            visitMsgShown = true;
            char head[96];
            int left = (int)g_state.pending.size();
            snprintf(head, sizeof(head), "%d rings delivered.", ringsNow);
            if (left > 0) {
                char more[128];
                snprintf(more, sizeof(more), fromStage
                    ? "%d item%s waiting. They arrive when you come in from stage select."
                    : "%d item%s waiting. Your garden is full. Use or feed some items, then come back to the lobby.",
                    left, left == 1 ? "" : "s");
                MsgShow(0xFF5EE6B0, "CounterChao", head, more);
            }
            else {
                MsgShow(0xFF5EE6B0, "CounterChao", head, "Spend them at the Black Market.");
            }
        }
        // The slots hold something the mod doesn't recognise. If rewards are waiting, say so instead of staying silent.
        static int badTicks = 0;          // half-seconds in the lobby with slots_ok=0
        static bool holdShown = false;    // once per launch
        if (lobbyArea && ok == 0) badTicks++; else badTicks = 0;
        bool waitingAny = g_netState == 2 && g_stateReady && (!g_state.pending.empty() || g_state.pendingRings > 0);
        if (badTicks >= 4 && waitingAny && !holdShown) {
            holdShown = true;
            Say("CounterChao: delivery on hold: a slot holds something unrecognised (see slots= in the status line)\n");
            MsgShow(0xFFFFC857, "CounterChao: delivery on hold", "Some items in your gardens aren't recognised.",
                "Nothing was changed. Pick them up or give them to a Chao, then come back to the lobby.");
        }
        // The once-per-launch message. With nothing waiting it doesn't depend on the slots.
        if (lobbyArea && (ok == 1 || (!waitingAny && badTicks >= 4)) && !g_msgShown && g_netState == 2) {
            g_msgShown = true;
            visitMsgShown = true;
            int waiting = (int)g_state.pending.size();
            char head[96];
            LONG outcome = g_outcome;
            if (outcome == OUT_SETUP) {
                MsgShow(0xFFFF7B72, "CounterChao: setup needed", "Your SteamID64 isn't set up.",
                    "Open the SA Mod Manager, select this mod, click Configure and enter your SteamID64.");
            }
            else if (outcome == OUT_STATE_BAD) {
                MsgShow(0xFFFF7B72, "CounterChao: state file problem", "state.json couldn't be read.",
                    "Nothing was changed or delivered. Details are in CounterChao.log.");
            }
            else if (outcome == OUT_NET_FAIL) {
                MsgShow(0xFFFF7B72, "CounterChao: can't reach Leetify", "No connection to Leetify.",
                    "Check your internet and restart the game. Details are in CounterChao.log.");
            }
            else if (outcome == OUT_HTTP_ERR) {
                DWORD http = g_netStatus;
                if (http == 429) {
                    MsgShow(0xFFFFC857, "CounterChao: Leetify is busy", "Too many requests right now.",
                        "Try again in a few minutes.");
                }
                else if (http >= 500 && http <= 599) {
                    MsgShow(0xFFFF7B72, "CounterChao: Leetify is having trouble", "Leetify's servers returned an error.",
                        "Try again later. Details are in CounterChao.log.");
                }
                else {
                    MsgShow(0xFFFF7B72, "CounterChao: no matches found", "Leetify didn't return your matches.",
                        "Check your SteamID64 in Configure and make sure your Leetify profile is public.");
                }
            }
            else if (outcome == OUT_NO_MATCHES) {
                MsgShow(0xFFFF7B72, "CounterChao: no matches found", "Leetify has no matches for this SteamID64.",
                    "Check your SteamID64 in Configure and make sure your Leetify profile is public.");
            }
            else if (outcome == OUT_PARSE) {
                MsgShow(0xFFFF7B72, "CounterChao: unexpected reply", "Leetify's reply couldn't be read.",
                    "Try again later. Details are in CounterChao.log.");
            }
            else if (outcome == OUT_SAVE_FAIL) {
                MsgShow(0xFFFF7B72, "CounterChao: something went wrong", "CounterChao couldn't save its state.",
                    "Nothing was delivered. Details are in CounterChao.log.");
            }
            else if (g_deliveredCount > 0) {
                snprintf(head, sizeof(head), "%d item%s delivered.", g_deliveredCount, g_deliveredCount == 1 ? "" : "s");
                if (waiting > 0) {
                    char more[96];
                    snprintf(more, sizeof(more), "%d more waiting. Free some space and come back to the lobby.", waiting);
                    MsgShow(0xFF5EE6B0, "CounterChao", head, more);
                }
                else {
                    MsgShow(0xFF5EE6B0, "CounterChao", head, "Walk into the garden to find them.");
                }
            }
            else if (waiting > 0) {
                snprintf(head, sizeof(head), "%d item%s waiting.", waiting, waiting == 1 ? "" : "s");
                MsgShow(0xFFFFC857, "CounterChao", head, fromStage
                    ? "They arrive when you come in from stage select."
                    : "Your garden is full. Use or feed some items, then come back to the lobby.");
            }
            else if (outcome == OUT_SKIPPED) {
                MsgShow(0xFF6CB6FF, "CounterChao", "Checked Leetify a moment ago.", "Restart the game in a few minutes to check again.");
            }
            else {
                MsgShow(0xFF6CB6FF, "CounterChao", "No new matches.", "Play a CS2 match, let Leetify pick it up, then restart the game.");
            }
        }
        // Later visits in the same launch: if items are still waiting and none could be placed, say why. Once per visit.
        else if (inLobby && !visitMsgShown && written == 0 && g_netState == 2 && g_stateReady && !g_state.pending.empty()) {
            visitMsgShown = true;
            int waiting = (int)g_state.pending.size();
            char head[96];
            snprintf(head, sizeof(head), "%d item%s waiting.", waiting, waiting == 1 ? "" : "s");
            MsgShow(0xFFFFC857, "CounterChao", head, fromStage
                ? "They arrive when you come in from stage select."
                : "Your garden is full. Use or feed some items, then come back to the lobby.");
        }

        char line[512];
        snprintf(line, sizeof(line), "slots_ok=%d free=%d GameState=%d Level=%d Area(cur/last/next)=%d/%d/%d rings=%d rings_save=%d slots=%s",
            ok, freeSlots, (int)GameState, (int)CurrentLevel,
            (int)CurrentChaoArea, (int)LastChaoArea, (int)NextChaoArea,
            (int)TotalRings, (int)TotalRings_Save, slots);
        if (strcmp(line, last) != 0) {
            Say("CounterChao: %s\n", line);
            strcpy_s(last, line);
        }
    }

    __declspec(dllexport) void __cdecl OnInput() {}
    __declspec(dllexport) void __cdecl OnControl() {}
    __declspec(dllexport) ModInfo SA2ModInfo = { ModLoaderVer };
}