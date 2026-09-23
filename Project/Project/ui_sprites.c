#include "ui.h"
#include "mech.h"
#include <math.h>

// Procedural mech sprites. The shape comes from the model's `look`, the
// colours from the model itself, so several models can share one silhouette.

static Color scaleColor(Color c, int num, int den) {
    return (Color) { (unsigned char)(c.r * num / den), (unsigned char)(c.g * num / den), (unsigned char)(c.b * num / den), 255 };
}

static void tri(float x1, float y1, float x2, float y2, float x3, float y3, Color c) {
    DrawTriangle((Vector2) { x1, y1 }, (Vector2) { x2, y2 }, (Vector2) { x3, y3 }, c);
}

// Overworld-sized mech (fits a 40x40 tile)
void drawMechOverworld(int model, int x, int y, int facingDir, float t) {
    const MechModel* m = &mechModels[model];
    Color body = m->body, accent = m->accent, glow = m->glow;
    float pulse = 0.5f + 0.5f * sinf(t * 4);

    // Legs
    DrawRectangle(x + 12, y + 28, 6, 10, scaleColor(body, 1, 2));
    DrawRectangle(x + 22, y + 28, 6, 10, scaleColor(body, 1, 2));
    // Body
    DrawRectangle(x + 11, y + 16, 18, 13, body);
    DrawRectangle(x + 11, y + 16, 18, 3, accent);
    // Shoulders
    DrawRectangle(x + 6, y + 18, 6, 6, scaleColor(body, 3, 4));
    DrawRectangle(x + 28, y + 18, 6, 6, scaleColor(body, 3, 4));
    // Head
    DrawRectangle(x + 14, y + 6, 12, 10, body);
    if (facingDir == 0)      DrawRectangle(x + 16, y + 11, 8, 3, accent);
    else if (facingDir == 1) DrawRectangle(x + 16, y + 7, 8, 2, scaleColor(accent, 1, 2));
    else if (facingDir == 2) DrawRectangle(x + 16, y + 10, 3, 4, accent);
    else                     DrawRectangle(x + 21, y + 10, 3, 4, accent);

    switch (m->look) {
    case 0:   // antenna
        DrawLine(x + 20, y + 6, x + 22, y - 2, accent);
        DrawCircle(x + 22, y - 2, 2, (Color) { glow.r, glow.g, glow.b, (unsigned char)(180 + pulse * 75) });
        break;
    case 1:   // helmet horn + wide chest
        DrawRectangle(x + 18, y + 1, 4, 4, accent);
        DrawRectangle(x + 8, y + 20, 24, 3, scaleColor(body, 3, 4));
        break;
    case 2:   // side fins
        tri(x + 14.0f, y + 8.0f, x + 6.0f, y + 2.0f, x + 14.0f, y + 14.0f, accent);
        tri(x + 26.0f, y + 8.0f, x + 34.0f, y + 2.0f, x + 26.0f, y + 14.0f, accent);
        break;
    case 3:   // blade arms
        tri(x + 8.0f, y + 22.0f, x + 2.0f, y + 28.0f, x + 8.0f, y + 28.0f, accent);
        tri(x + 32.0f, y + 22.0f, x + 38.0f, y + 28.0f, x + 32.0f, y + 28.0f, accent);
        break;
    case 4:   // shoulder missiles
        DrawRectangle(x + 4, y + 12, 4, 8, accent);
        DrawRectangle(x + 32, y + 12, 4, 8, accent);
        break;
    case 5:   // horns
        tri(x + 12.0f, y + 6.0f, x + 8.0f, y - 4.0f, x + 16.0f, y + 6.0f, (Color) { 220, 30, 60, 255 });
        tri(x + 28.0f, y + 6.0f, x + 32.0f, y - 4.0f, x + 24.0f, y + 6.0f, (Color) { 220, 30, 60, 255 });
        break;
    }

    // Chest core
    DrawCircle(x + 20, y + 22, 3, (Color) { glow.r, glow.g, glow.b, (unsigned char)(150 + pulse * 100) });
}

// Battle-sized mech centred on (cx, cy); s is a scale in tenths
void drawMechBattle(int model, int cx, int cy, int s, int isEnemy) {
    const MechModel* m = &mechModels[model];
    Color body = m->body, accent = m->accent, glow = m->glow;
    if (isEnemy) {
        // tint slightly reddish for hostiles
        body = (Color){ (unsigned char)fminf(255, body.r * 0.9f + 40), (unsigned char)(body.g * 0.7f),
                        (unsigned char)(body.b * 0.7f), 255 };
        accent = (Color){ (unsigned char)fminf(255, accent.r * 0.9f + 30), (unsigned char)(accent.g * 0.7f),
                          (unsigned char)(accent.b * 0.7f), 255 };
    }
    float pulse = 0.5f + 0.5f * sinf(glowTimer * 4);
    float breathe = 1.0f + 0.03f * sinf(glowTimer * 3 + model);
    int sc = (int)(s * breathe);
#define P(v) ((v) * sc / 10)
#define F(v) ((float)((v) * sc / 10))

    // Legs
    DrawRectangle(cx - P(22), cy + P(18), P(10), P(24), scaleColor(body, 1, 2));
    DrawRectangle(cx + P(12), cy + P(18), P(10), P(24), scaleColor(body, 1, 2));
    DrawRectangle(cx - P(22), cy + P(34), P(10), P(8), scaleColor(body, 1, 3));
    DrawRectangle(cx + P(12), cy + P(34), P(10), P(8), scaleColor(body, 1, 3));
    // Torso
    DrawRectangle(cx - P(24), cy - P(10), P(48), P(32), body);
    DrawRectangle(cx - P(24), cy - P(10), P(48), P(5), accent);
    // Shoulders
    DrawRectangle(cx - P(38), cy - P(8), P(14), P(16), accent);
    DrawRectangle(cx + P(24), cy - P(8), P(14), P(16), accent);
    // Head
    DrawRectangle(cx - P(18), cy - P(30), P(36), P(22), body);
    DrawRectangle(cx - P(18), cy - P(30), P(36), P(4), accent);
    // Visor (always glowing)
    DrawRectangle(cx - P(14), cy - P(22), P(28), P(6), accent);
    DrawRectangle(cx - P(14), cy - P(22), P(28), P(2), (Color) { 255, 255, 255, 220 });
    // Arms
    DrawRectangle(cx - P(46), cy - P(4), P(12), P(20), scaleColor(body, 3, 4));
    DrawRectangle(cx + P(34), cy - P(4), P(12), P(20), scaleColor(body, 3, 4));

    Color red = { 220, 30, 60, 255 };
    switch (m->look) {
    case 0:   // antenna
        DrawLine(cx + P(4), cy - P(30), cx + P(12), cy - P(46), accent);
        DrawCircle(cx + P(12), cy - P(46), F(3), (Color) { glow.r, glow.g, glow.b, (unsigned char)(180 + pulse * 75) });
        break;
    case 1:   // horn, chest plate, armoured fists
        DrawRectangle(cx - P(4), cy - P(40), P(8), P(12), accent);
        DrawRectangle(cx - P(24), cy - P(2), P(48), P(4), scaleColor(body, 3, 4));
        DrawRectangle(cx - P(50), cy + P(12), P(16), P(8), accent);
        DrawRectangle(cx + P(34), cy + P(12), P(16), P(8), accent);
        break;
    case 2:   // side fins, twin antennae
        tri(cx - F(24), cy - F(8), cx - F(40), cy - F(20), cx - F(24), cy + F(4), accent);
        tri(cx + F(24), cy - F(8), cx + F(40), cy - F(20), cx + F(24), cy + F(4), accent);
        DrawLine(cx - P(8), cy - P(30), cx - P(16), cy - P(46), accent);
        DrawLine(cx + P(8), cy - P(30), cx + P(16), cy - P(46), accent);
        break;
    case 3:   // blade arms, head crest
        tri(cx - F(46), cy - F(4), cx - F(62), cy + F(14), cx - F(42), cy + F(6), accent);
        tri(cx + F(46), cy - F(4), cx + F(62), cy + F(14), cx + F(42), cy + F(6), accent);
        DrawRectangle(cx - P(4), cy - P(42), P(8), P(14), accent);
        break;
    case 4:   // shoulder missile pods, big missile on back
        DrawRectangle(cx - P(48), cy - P(18), P(10), P(12), (Color) { 40, 40, 60, 255 });
        DrawRectangle(cx + P(38), cy - P(18), P(10), P(12), (Color) { 40, 40, 60, 255 });
        DrawCircle(cx - P(43), cy - P(14), F(2), (Color) { 255, 120, 60, (unsigned char)(150 + pulse * 100) });
        DrawCircle(cx + P(43), cy - P(14), F(2), (Color) { 255, 120, 60, (unsigned char)(150 + pulse * 100) });
        DrawRectangle(cx - P(6), cy - P(46), P(12), P(18), (Color) { 60, 60, 80, 255 });
        tri(cx - F(6), cy - F(46), (float)cx, cy - F(54), cx + F(6), cy - F(46), (Color) { 220, 90, 90, 255 });
        break;
    case 5:   // horns, spikes, big glowing core
        tri(cx - F(18), cy - F(30), cx - F(40), cy - F(54), cx - F(8), cy - F(30), red);
        tri(cx + F(18), cy - F(30), cx + F(40), cy - F(54), cx + F(8), cy - F(30), red);
        tri(cx - F(40), cy - F(8), cx - F(58), cy - F(24), cx - F(30), cy - F(8), red);
        tri(cx + F(40), cy - F(8), cx + F(58), cy - F(24), cx + F(30), cy - F(8), red);
        DrawCircle(cx, cy + P(6), F(12), (Color) { 80, 10, 20, 255 });
        DrawCircle(cx, cy + P(6), F(8), (Color) { glow.r, glow.g, glow.b, (unsigned char)(200 + pulse * 55) });
        DrawCircle(cx, cy + P(6), F(4), (Color) { 255, 220, 220, 255 });
        DrawCircleLines(cx, cy, F(60), (Color) { glow.r, glow.g, glow.b, (unsigned char)(40 + pulse * 40) });
        break;
    }

    if (m->look != 5)
        DrawCircle(cx, cy + P(6), F(5), (Color) { glow.r, glow.g, glow.b, (unsigned char)(180 + pulse * 75) });
    DrawCircleLines(cx, cy - P(20), F(6), WHITE);
#undef P
#undef F
}
