// CounterChao_msg.h - Tier 1 on-screen message (debug-text based), v2.
// Include AFTER pch.h / SA2ModLoader.h in main.cpp.
// Same API as v1: MsgInit(helpers), MsgShow(argb,title,line1,line2), MsgFrame().
#pragma once
#include <string.h>
#include <stdio.h>

#ifndef NJM_LOCATION
#define NJM_LOCATION(x, y) (((int)(x) << 16) | ((int)(y) & 0xFFFF))
#endif

// ---- tweakables ----
static const bool  MSG_SHOW_GRID  = false;  // yellow coordinate grid
static const bool  MSG_SHOW_DEBUG = false;  // prints the resolution values top-left (for testing)
static const float MSG_BASE_SIZE  = 20.0f;  // font size at 600 px screen height
static const int   MSG_SECONDS    = 12;
static const int   MSG_FPS        = 60;
static const int   MSG_MAX_CHARS  = 40;     // never wrap wider than this
static const int   MSG_MAX_LINES  = 8;

struct MsgState {
    char title[64];
    char text1[160];
    char text2[160];
    int  framesLeft;
    unsigned color;
    bool haveHelpers;
};
static MsgState g_msg = { "", "", "", 0, 0xFFFFFFFF, false };
static HelperFunctions g_msgHelpers;

static void MsgInit(const HelperFunctions& h) {
    if (h.Version >= 8) { g_msgHelpers = h; g_msg.haveHelpers = true; }
}

static void MsgShow(unsigned argb, const char* title, const char* l1, const char* l2) {
    strncpy_s(g_msg.title, title ? title : "", _TRUNCATE);
    strncpy_s(g_msg.text1, l1 ? l1 : "", _TRUNCATE);
    strncpy_s(g_msg.text2, l2 ? l2 : "", _TRUNCATE);
    g_msg.color = argb;
    g_msg.framesLeft = MSG_SECONDS * MSG_FPS;
}

// Word-wrap src into lines[] (each <= maxChars). Returns number of lines added.
static int MsgWrap(const char* src, int maxChars, char lines[][64], int start) {
    int n = start;
    int len = (int)strlen(src);
    int i = 0;
    while (i < len && n < MSG_MAX_LINES) {
        while (i < len && src[i] == ' ') i++;
        if (i >= len) break;
        int end = i + maxChars;
        if (end >= len) end = len;
        else {
            int cut = end;
            while (cut > i && src[cut] != ' ') cut--;
            if (cut > i) end = cut;
        }
        int cnt = end - i;
        if (cnt > 63) cnt = 63;
        memcpy(lines[n], src + i, cnt);
        lines[n][cnt] = 0;
        n++;
        i = end;
    }
    return n;
}

// Call EVERY frame from OnFrame. Only draws inside Chao World.
static void MsgFrame() {
    if (!g_msg.haveHelpers) return;
    if ((int)CurrentLevel != 90 || (int)GameState != 16) return;
    HelperFunctions& h = g_msgHelpers;

    float W = HorizontalResolution, H = VerticalResolution;
    if (!(W >= 200.0f && W <= 10000.0f && H >= 200.0f && H <= 10000.0f)) { W = 800.0f; H = 600.0f; }
    float scale = H / 600.0f;
    float size = MSG_BASE_SIZE * scale;

    if (MSG_SHOW_DEBUG) {
        char b[96];
        sprintf_s(b, "res %.0fx%.0f  float %.0fx%.0f", (double)HorizontalResolution, (double)VerticalResolution,
                  (double)HorizontalResolution_Float, (double)VerticalResolution_Float);
        h.SetDebugFontSize(12.0f);
        h.SetDebugFontColor(0xFFFFFFFF);
        h.DisplayDebugString(NJM_LOCATION(0, 0), b);
    }
    if (MSG_SHOW_GRID) {
        h.SetDebugFontSize(12.0f);
        h.SetDebugFontColor(0xFFFFFF00);
        for (int x = 0; x <= 160; x += 10)
            for (int y = 0; y <= 90; y += 5) {
                char b[16];
                sprintf_s(b, "%d,%d", x, y);
                h.DisplayDebugString(NJM_LOCATION(x, y), b);
            }
    }

    if (g_msg.framesLeft <= 0) return;
    g_msg.framesLeft--;

    // One location unit = one font-size cell, so the font size already does the scaling.
    // Column/row/step are therefore plain constants, NOT multiplied by scale.
    const int col = 2, row = 3, step = 2;
    int maxChars = (int)(W / size) - 2 * col;
    if (maxChars > MSG_MAX_CHARS) maxChars = MSG_MAX_CHARS;
    if (maxChars < 12) maxChars = 12;

    char lines[MSG_MAX_LINES][64];
    int head = MsgWrap(g_msg.text1, maxChars, lines, 0);   // lines [0,head) = headline (colored)
    int total = MsgWrap(g_msg.text2, maxChars, lines, head);

    h.SetDebugFontSize(size);
    h.SetDebugFontColor(0xFFFFFFFF);
    h.DisplayDebugString(NJM_LOCATION(col, row), g_msg.title);
    int r = row + step + 1;
    for (int i = 0; i < total; i++) {
        h.SetDebugFontColor(i < head ? (int)g_msg.color : (int)0xFFE9F3F1);
        h.DisplayDebugString(NJM_LOCATION(col, r), lines[i]);
        r += step;
    }
}
