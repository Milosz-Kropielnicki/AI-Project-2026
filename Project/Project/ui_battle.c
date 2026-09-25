#include "ui.h"
#include "game.h"
#include "battle.h"
#include "world.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ============ EFFECTS ============
#define MAX_PARTICLES 512
typedef struct { Vector2 pos, vel; float life, maxLife, size, gravity; Color color; int active; } Particle;
static Particle particles[MAX_PARTICLES];

typedef struct {
    int active, kind;
    float t, duration;
    Vector2 from, to;
    Color color;
    int damageShown, damage, hit;
    int munition, armorDamage, integrityDamage, lethal;
} Effect;
#define MAX_EFFECTS 8
static Effect effects[MAX_EFFECTS];

// Floating combat text: Integrity damage (red), Armor damage (blue), MISS, SCRAPPED
typedef struct { int active; Vector2 pos; float life; char text[24]; int size; Color color; } DamageNum;
#define MAX_DAMAGE_NUMS 16
static DamageNum damageNums[MAX_DAMAGE_NUMS];

static float shakeAmount = 0, shakeTimer = 0;
static const Vector2 playerMechPos = { 160, 300 };
static const Vector2 enemyMechPos = { 520, 150 };

static void spawnParticleG(Vector2 pos, Vector2 vel, float life, float size, Color c, float gravity) {
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!particles[i].active) { particles[i] = (Particle){ pos, vel, life, life, size, gravity, c, 1 }; return; }
}

static void spawnParticle(Vector2 pos, Vector2 vel, float life, float size, Color c) {
    spawnParticleG(pos, vel, life, size, c, 60);
}

static float frandRange(float lo, float hi) { return lo + (float)GetRandomValue(0, 1000) / 1000.0f * (hi - lo); }

static void spawnBurst(Vector2 pos, int count, Color c, float smin, float smax, float life) {
    for (int i = 0; i < count; i++) {
        float ang = (float)GetRandomValue(0, 360) * DEG2RAD;
        float sp = smin + (float)GetRandomValue(0, 100) / 100.0f * (smax - smin);
        Vector2 v = { cosf(ang) * sp, sinf(ang) * sp };
        spawnParticle(pos, v, life * (0.6f + 0.4f * (float)GetRandomValue(0, 100) / 100.0f),
            2.0f + (float)GetRandomValue(0, 4), c);
    }
}

static float effectDuration(int kind) {
    switch (kind) {
    case FX_NOVA: return 0.9f;
    case FX_MISSILE: return 0.7f;
    case FX_BEAM: return 0.5f;
    case FX_SCAN: return 1.0f;
    default: return 0.55f;
    }
}

static void addEffect(const BattleEvent* ev, Vector2 from, Vector2 to) {
    for (int i = 0; i < MAX_EFFECTS; i++)
        if (!effects[i].active) {
            effects[i] = (Effect){ 1, ev->fx, 0, effectDuration(ev->fx), from, to, ev->color, 0, ev->damage, ev->hit,
                ev->munition, ev->armorDamage, ev->integrityDamage, ev->lethal };
            return;
        }
}

static void addFloatText(Vector2 pos, const char* text, int size, Color c) {
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++)
        if (!damageNums[i].active) {
            damageNums[i] = (DamageNum){ 1, pos, 1.2f, "", size, c };
            snprintf(damageNums[i].text, sizeof(damageNums[i].text), "%s", text);
            return;
        }
}

// Particles that say what hit: sparks, glowing motes, rising embers, arcs,
// fireball and smoke, dripping chemicals.
static void munitionBurst(Vector2 at, int munition, Color c) {
    switch (munition) {
    case MUN_BALLISTIC:
        for (int i = 0; i < 20; i++) {
            float a = frandRange(0, 2 * PI), sp = frandRange(160, 400);
            spawnParticleG(at, (Vector2) { cosf(a) * sp, sinf(a) * sp - 60 }, 0.35f, 2, (Color) { 255, 230, 150, 255 }, 600);
        }
        break;
    case MUN_ENERGY:
        for (int i = 0; i < 16; i++) {
            float a = frandRange(0, 2 * PI), sp = frandRange(20, 90);
            spawnParticleG(at, (Vector2) { cosf(a) * sp, sinf(a) * sp - 30 }, 0.9f, 4, c, -20);
        }
        break;
    case MUN_THERMAL:
        for (int i = 0; i < 24; i++) {
            Color ember = GetRandomValue(0, 1) ? (Color) { 255, 140, 40, 255 } : (Color) { 255, 70, 30, 255 };
            spawnParticleG((Vector2) { at.x + frandRange(-30, 30), at.y + frandRange(-20, 20) },
                (Vector2) { frandRange(-50, 50), frandRange(-150, -40) }, 1.1f, 3, ember, -40);
        }
        break;
    case MUN_ELECTROMAGNETIC:
        for (int i = 0; i < 22; i++) {
            float a = frandRange(0, 2 * PI), sp = frandRange(200, 440);
            spawnParticleG(at, (Vector2) { cosf(a) * sp, sinf(a) * sp }, 0.18f, 2,
                GetRandomValue(0, 2) ? c : WHITE, 0);
        }
        break;
    case MUN_EXPLOSIVE:
        spawnBurst(at, 26, (Color) { 255, 170, 70, 255 }, 80, 300, 0.7f);
        for (int i = 0; i < 12; i++)
            spawnParticleG((Vector2) { at.x + frandRange(-25, 25), at.y + frandRange(-15, 15) },
                (Vector2) { frandRange(-30, 30), frandRange(-60, -20) }, 1.5f, 8, (Color) { 90, 90, 100, 160 }, -10);
        break;
    case MUN_CHEMICAL:
        for (int i = 0; i < 18; i++)
            spawnParticleG((Vector2) { at.x + frandRange(-20, 20), at.y },
                (Vector2) { frandRange(-90, 90), frandRange(-140, -30) }, 0.9f, 3, c, 420);
        break;
    default: break;
    }
}

static void shakeScreen(float amount, float time) {
    if (amount > shakeAmount) { shakeAmount = amount; shakeTimer = time; }
}

// Impact: munition particles and sound, Armor and Integrity numbers kept
// apart so the split is readable, shake - or a MISS marker.
static void impact(Effect* e, int burst, float smin, float smax, float life, float shake, float shakeTime, float missOffset) {
    if (e->hit) {
        if (burst > 0) spawnBurst(e->to, burst / 2, e->color, smin, smax, life);
        munitionBurst(e->to, e->munition, e->color);
        sfxImpact(e->munition);
        if (e->integrityDamage > 0) {
            addFloatText((Vector2) { e->to.x - 10, e->to.y - 44 }, TextFormat("-%d", e->integrityDamage), 28, (Color) { 255, 90, 90, 255 });
            sfxPlay(SFX_INTEGRITY_HIT);
        }
        if (e->armorDamage > 0) {
            addFloatText((Vector2) { e->to.x + 50, e->to.y - 18 }, TextFormat("-%d ARM", e->armorDamage), 18, (Color) { 120, 190, 255, 255 });
            if (e->integrityDamage == 0) sfxPlay(SFX_ARMOR_HIT);
        }
        if (e->lethal) {
            addFloatText((Vector2) { e->to.x, e->to.y - 84 }, "SCRAPPED", 30, (Color) { 255, 220, 100, 255 });
            sfxPlay(SFX_SCRAPPED);
        }
        if (e->damage > 0) shakeScreen(shake, shakeTime);
    }
    else {
        addFloatText((Vector2) { e->to.x + missOffset, e->to.y - 40 }, "MISS", 28, (Color) { 200, 200, 200, 255 });
        sfxPlay(SFX_MISS);
    }
}

static void updateEffects(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &particles[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0) { p->active = 0; continue; }
        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->vel.x *= 0.96f;
        p->vel.y *= 0.96f;
        p->vel.y += p->gravity * dt;
    }
    for (int i = 0; i < MAX_EFFECTS; i++) {
        Effect* e = &effects[i];
        if (!e->active) continue;
        e->t += dt;
        float p = e->t / e->duration; if (p > 1) p = 1;
        Vector2 cur = { e->from.x + (e->to.x - e->from.x) * p, e->from.y + (e->to.y - e->from.y) * p };
        switch (e->kind) {
        case FX_PULSE:
            if (p < 0.7f) spawnParticle(cur, (Vector2) { 0, 0 }, 0.25f, 4, e->color);
            if (!e->damageShown && p >= 0.7f) { e->damageShown = 1; impact(e, 12, 60, 200, 0.5f, 6, 0.25f, 0); }
            break;
        case FX_BEAM:
            if (p < 0.8f)
                spawnParticle(cur, (Vector2) { (float)GetRandomValue(-40, 40), (float)GetRandomValue(-40, 40) }, 0.2f, 3, e->color);
            if (!e->damageShown && p >= 0.6f) { e->damageShown = 1; impact(e, 20, 80, 260, 0.6f, 8, 0.3f, 40); }
            break;
        case FX_SCAN:
            if (!e->damageShown && p >= 0.9f) {
                e->damageShown = 1;
                spawnBurst(e->to, 24, (Color) { 120, 255, 220, 255 }, 30, 90, 0.8f);
            }
            break;
        case FX_NOVA:
            if (p < 0.5f) {
                if (GetRandomValue(0, 100) < 60)
                    spawnParticle(e->from, (Vector2) { (float)GetRandomValue(-120, 120), (float)GetRandomValue(-120, 120) },
                        0.35f, 3, (Color) { 200, 220, 255, 255 });
            }
            else if (!e->damageShown && p >= 0.55f) {
                e->damageShown = 1;
                spawnBurst(e->to, 80, (Color) { 140, 220, 255, 255 }, 100, 500, 1.0f);
                spawnBurst(e->to, 40, WHITE, 50, 400, 0.8f);
                if (e->hit) impact(e, 0, 0, 0, 0, 22, 0.6f, 0);
                else { impact(e, 0, 0, 0, 0, 0, 0, 60); shakeScreen(12, 0.4f); }
            }
            break;
        case FX_ARC:
            if (p < 0.9f && GetRandomValue(0, 100) < 70) {
                float jx = (float)GetRandomValue(-25, 25), jy = (float)GetRandomValue(-25, 25);
                spawnParticle((Vector2) { cur.x + jx, cur.y + jy },
                    (Vector2) { (float)GetRandomValue(-80, 80), (float)GetRandomValue(-80, 80) }, 0.25f, 3, e->color);
            }
            if (!e->damageShown && p >= 0.7f) { e->damageShown = 1; impact(e, 18, 100, 300, 0.5f, 7, 0.25f, 0); }
            break;
        case FX_BLADE:
            if (!e->damageShown && p >= 0.5f) { e->damageShown = 1; impact(e, 22, 120, 320, 0.5f, 9, 0.28f, 40); }
            break;
        case FX_JAM:
            if (!e->damageShown && p >= 0.85f) {
                e->damageShown = 1;
                spawnBurst(e->to, 14, e->color, 30, 100, 0.7f);
                if (!e->hit) { addFloatText((Vector2) { e->to.x, e->to.y - 40 }, "MISS", 28, (Color) { 200, 200, 200, 255 }); sfxPlay(SFX_MISS); }
                else if (e->munition >= 0) sfxImpact(e->munition);
            }
            break;
        case FX_MISSILE: {
            float arc = sinf(p * PI) * 120;
            Vector2 mpos = { cur.x, cur.y - arc };
            if (p < 0.9f && GetRandomValue(0, 100) < 80)
                spawnParticle(mpos, (Vector2) { 0, 0 }, 0.3f, 4, (Color) { 255, 180, 120, 255 });
            if (!e->damageShown && p >= 0.9f) {
                e->damageShown = 1;
                spawnBurst(e->to, 40, (Color) { 255, 180, 90, 255 }, 80, 320, 0.8f);
                spawnBurst(e->to, 20, (Color) { 255, 240, 180, 255 }, 40, 200, 0.6f);
                impact(e, 0, 0, 0, 0, 14, 0.4f, 50);
            }
            break;
        }
        }
        if (e->t >= e->duration) e->active = 0;
    }
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++) {
        if (!damageNums[i].active) continue;
        damageNums[i].life -= dt;
        damageNums[i].pos.y -= 40 * dt;
        if (damageNums[i].life <= 0) damageNums[i].active = 0;
    }
    if (shakeTimer > 0) { shakeTimer -= dt; if (shakeTimer <= 0) shakeAmount = 0; }
}

static void clearEffects(void) {
    memset(particles, 0, sizeof(particles));
    memset(effects, 0, sizeof(effects));
    memset(damageNums, 0, sizeof(damageNums));
    shakeAmount = 0;
    shakeTimer = 0;
}

static void drawEffects(void) {
    for (int i = 0; i < MAX_EFFECTS; i++) {
        Effect* e = &effects[i];
        if (!e->active) continue;
        float p = e->t / e->duration; if (p > 1) p = 1;
        Vector2 cur = { e->from.x + (e->to.x - e->from.x) * p, e->from.y + (e->to.y - e->from.y) * p };
        Color faint = { e->color.r, e->color.g, e->color.b, 90 };
        switch (e->kind) {
        case FX_PULSE: {
            float sz = 6 + 4 * sinf(e->t * 30);
            DrawCircleV(cur, sz + 6, (Color) { e->color.r, e->color.g, e->color.b, 80 });
            DrawCircleV(cur, sz, e->color);
            DrawCircleV(cur, sz * 0.5f, WHITE);
            break;
        }
        case FX_BEAM:
            if (p < 0.85f) {
                float w = 10 * (1 - p); if (w < 2) w = 2;
                DrawLineEx(e->from, e->to, w + 6, faint);
                DrawLineEx(e->from, e->to, w, e->color);
                DrawLineEx(e->from, e->to, w * 0.5f, WHITE);
            }
            else {
                float r = (p - 0.85f) * 6.6f * 30;
                DrawCircleV(e->to, r, (Color) { e->color.r, e->color.g, e->color.b, (unsigned char)(255 * (1 - p) * 6) });
            }
            break;
        case FX_SCAN: {
            float r = p * 80;
            DrawCircleLines((int)e->to.x, (int)e->to.y, r, e->color);
            DrawCircleLines((int)e->to.x, (int)e->to.y, r * 0.7f, (Color) { e->color.r, e->color.g, e->color.b, 180 });
            DrawCircleLines((int)e->to.x, (int)e->to.y, r * 0.4f, (Color) { e->color.r, e->color.g, e->color.b, 120 });
            const char* txt = "INTRUSION...";
            DrawText(txt, (int)(e->to.x - MeasureText(txt, 16) / 2), (int)(e->to.y - 90), 16, (Color) { 200, 255, 240, 255 });
            break;
        }
        case FX_NOVA:
            if (p < 0.5f) {
                float r = p * 2 * 30;
                DrawCircleV(e->from, r + 4, (Color) { e->color.r, e->color.g, e->color.b, 80 });
                DrawCircleV(e->from, r, e->color);
                DrawCircleV(e->from, r * 0.5f, WHITE);
            }
            else {
                float ep = (p - 0.5f) * 2;
                float r = ep * 160;
                unsigned char a = (unsigned char)(255 * (1 - ep));
                DrawCircleV(e->to, r, (Color) { e->color.r, e->color.g, e->color.b, a });
                DrawCircleV(e->to, r * 0.7f, (Color) { 255, 255, 255, a });
                DrawCircleV(e->to, r * 0.4f, WHITE);
                DrawCircleLines((int)e->to.x, (int)e->to.y, r, WHITE);
                DrawCircleLines((int)e->to.x, (int)e->to.y, r * 1.3f, (Color) { 220, 240, 255, (unsigned char)(a * 0.6f) });
            }
            break;
        case FX_ARC: {
            int segments = 8;
            Vector2 prev = e->from;
            for (int s = 1; s <= segments; s++) {
                float sp = (float)s / segments;
                Vector2 target = { e->from.x + (e->to.x - e->from.x) * sp * p, e->from.y + (e->to.y - e->from.y) * sp * p };
                if (s < segments) {
                    target.x += (float)GetRandomValue(-18, 18);
                    target.y += (float)GetRandomValue(-18, 18);
                }
                DrawLineEx(prev, target, 5, (Color) { e->color.r, e->color.g, e->color.b, 100 });
                DrawLineEx(prev, target, 2, e->color);
                DrawLineEx(prev, target, 1, WHITE);
                prev = target;
            }
            if (p >= 0.7f) {
                float ep = (p - 0.7f) / 0.3f;
                DrawCircleV(e->to, ep * 60, (Color) { 255, 255, 200, (unsigned char)(255 * (1 - ep)) });
            }
            break;
        }
        case FX_BLADE: {
            float ang = p * PI;
            for (int dir = -1; dir <= 1; dir += 2) {
                Vector2 a1 = { e->to.x + cosf(ang + dir * 0.3f) * 60, e->to.y + sinf(ang + dir * 0.3f) * 60 };
                Vector2 a2 = { e->to.x + cosf(ang + dir * 0.6f) * 70, e->to.y + sinf(ang + dir * 0.6f) * 70 };
                DrawLineEx(a1, a2, 6, (Color) { e->color.r, e->color.g, e->color.b, 180 });
                DrawLineEx(a1, a2, 3, e->color);
                DrawLineEx(a1, a2, 1, WHITE);
            }
            if (p > 0.3f && p < 0.7f)
                DrawCircleLines((int)e->to.x, (int)e->to.y, 40 + (p - 0.3f) / 0.4f * 30, e->color);
            break;
        }
        case FX_JAM:
            for (int k = 0; k < 3; k++) {
                float rp = p - k * 0.15f;
                if (rp > 0 && rp < 1)
                    DrawCircleLines((int)e->to.x, (int)e->to.y, rp * 120,
                        (Color) { e->color.r, e->color.g, e->color.b, (unsigned char)(255 * (1 - rp)) });
            }
            for (int k = 0; k < 6; k++) {
                int ny = (int)e->to.y + GetRandomValue(-50, 50);
                DrawLine((int)e->to.x + GetRandomValue(-60, 0), ny, (int)e->to.x + GetRandomValue(0, 60), ny,
                    (Color) { e->color.r, e->color.g, e->color.b, 120 });
            }
            break;
        case FX_MISSILE: {
            Vector2 mpos = { cur.x, cur.y - sinf(p * PI) * 120 };
            if (p < 0.9f) {
                DrawCircleV(mpos, 8, e->color);
                DrawCircleV(mpos, 5, (Color) { 255, 240, 200, 255 });
                DrawCircleLines((int)mpos.x, (int)mpos.y, 10, WHITE);
                for (int k = 0; k < 3; k++) {
                    float ofs = (k + 1) * 6.0f;
                    DrawCircleV((Vector2) { mpos.x - ofs, mpos.y + ofs * 0.5f }, 6.0f - k * 2,
                        (Color) { 255, 180, 80, (unsigned char)(200 - k * 60) });
                }
            }
            else {
                float ep = (p - 0.9f) / 0.1f;
                float r = 40 + ep * 80;
                unsigned char a = (unsigned char)(255 * (1 - ep));
                DrawCircleV(e->to, r, (Color) { 255, 160, 60, a });
                DrawCircleV(e->to, r * 0.6f, (Color) { 255, 240, 180, a });
                DrawCircleV(e->to, r * 0.3f, (Color) { 255, 255, 255, a });
            }
            break;
        }
        }
    }
}

static void drawParticles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle* p = &particles[i];
        if (!p->active) continue;
        float a = p->life / p->maxLife;
        Color c = p->color;
        c.a = (unsigned char)(c.a * a);
        DrawCircleV(p->pos, p->size * a + 0.5f, c);
    }
}

static void drawDamageNums(void) {
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++) {
        DamageNum* d = &damageNums[i];
        if (!d->active) continue;
        Color c = d->color;
        c.a = (unsigned char)(255 * d->life / 1.2f);
        int w = MeasureText(d->text, d->size);
        DrawText(d->text, (int)(d->pos.x - w / 2 + 2), (int)d->pos.y + 2, d->size, (Color) { 0, 0, 0, c.a });
        DrawText(d->text, (int)(d->pos.x - w / 2), (int)d->pos.y, d->size, c);
    }
}

// ============ TOOLTIPS ============
// Hover anything with a number on it to see what it means. Hot areas are
// registered while drawing; the first one under the mouse wins and is drawn
// last, on top of everything.
static struct {
    int active;
    char title[64];
    char body[400];
    Explanation why;
    int hasWhy;
    int fixed;          // keyboard info box: drawn at a fixed spot, not at the mouse
} tip;

// Wraps text to width; draws it when draw is set. Returns the lines used.
static int wrapText(const char* text, int x, int y, int width, int size, Color c, int draw) {
    char line[200] = "";
    int lines = 0;
    const char* p = text;
    while (*p) {
        const char* sp = strchr(p, ' ');
        int len = sp ? (int)(sp - p) : (int)strlen(p);
        char trial[200];
        snprintf(trial, sizeof(trial), "%s%s%.*s", line, line[0] ? " " : "", len, p);
        if (MeasureText(trial, size) > width && line[0]) {
            if (draw) DrawText(line, x, y + lines * (size + 3), size, c);
            lines++;
            snprintf(line, sizeof(line), "%.*s", len, p);
        }
        else snprintf(line, sizeof(line), "%s", trial);
        p += len;
        while (*p == ' ') p++;
    }
    if (line[0]) { if (draw) DrawText(line, x, y + lines * (size + 3), size, c); lines++; }
    return lines;
}

static void tipText(Rectangle hot, const char* title, const char* body) {
    if (tip.active || !mouseOver(hot)) return;
    tip.active = 1;
    tip.fixed = 0;
    tip.hasWhy = 0;
    snprintf(tip.title, sizeof(tip.title), "%s", title);
    snprintf(tip.body, sizeof(tip.body), "%s", body ? body : "");
}

static void tipWhy(Rectangle hot, const char* title, const char* body, const Explanation* why) {
    if (tip.active || !mouseOver(hot)) return;
    tipText(hot, title, body);
    tip.why = *why;
    tip.hasWhy = 1;
}

static void tipDraw(void) {
    if (!tip.active) return;
    const int w = 520, pad = 8, size = 10;
    int h = pad * 2 + 18;
    if (tip.body[0]) h += wrapText(tip.body, 0, 0, w - pad * 2, size, WHITE, 0) * (size + 3) + 4;
    if (tip.hasWhy)
        for (int i = 0; i < tip.why.n; i++) h += wrapText(tip.why.line[i], 0, 0, w - pad * 2, size, WHITE, 0) * (size + 3);
    Vector2 m = layoutMouse();
    int x = tip.fixed ? SCREEN_W / 2 - w / 2 : (int)m.x + 16, y = tip.fixed ? 60 : (int)m.y + 16;
    if (x + w > SCREEN_W - 4) x = SCREEN_W - 4 - w;
    if (y + h > SCREEN_H - 4) y = (tip.fixed ? SCREEN_H - 4 : (int)m.y - 8) - h;
    if (y < 4) y = 4;
    DrawRectangle(x, y, w, h, (Color) { 8, 14, 28, 255 });
    DrawRectangleLines(x, y, w, h, (Color) { 120, 220, 255, 230 });
    DrawText(tip.title, x + pad, y + pad, 14, (Color) { 150, 230, 255, 255 });
    int ly = y + pad + 18;
    if (tip.body[0]) ly += wrapText(tip.body, x + pad, ly, w - pad * 2, size, (Color) { 220, 230, 240, 255 }, 1) * (size + 3) + 4;
    if (tip.hasWhy)
        for (int i = 0; i < tip.why.n; i++)
            ly += wrapText(tip.why.line[i], x + pad, ly, w - pad * 2, size,
                tip.why.warn[i] ? (Color) { 255, 150, 110, 255 } : (Color) { 190, 225, 200, 255 }, 1) * (size + 3);
}

static const char* munitionPlain(int m) {
    switch (m) {
    case MUN_BALLISTIC:       return "BALLISTIC: reliable, moderate armor penetration.";
    case MUN_ENERGY:          return "ENERGY: very accurate, but Armor stops most of it.";
    case MUN_THERMAL:         return "THERMAL: close range, low penetration, runs hot.";
    case MUN_ELECTROMAGNETIC: return "ELECTROMAGNETIC: little or no damage - it scrambles the target's systems instead.";
    case MUN_EXPLOSIVE:       return "EXPLOSIVE: heavy damage with moderate penetration, limited ammo.";
    default:                  return "CHEMICAL: low damage and penetration, eats away at the target.";
    }
}

// ============ BATTLE SCREEN ============
static int weaponSel = 0;
static int reselectAfterShot = 0;
static int formulaView = 0;     // [V] preview panel: plain summary / full formula
static int infoOpen = 0;        // [I] full breakdown of the selected weapon
static int logOpen = 0, logSel = 0;
static int lastPhase = -1, heatWasCritical = 0;
static const LogEntry* lastSeenLog = NULL;

// Layout: HUDs on top, log strip, then a bottom panel with the weapon list on
// the left and the formula preview for the selected weapon on the right.
#define PANEL_Y (SCREEN_H - 180)
#define ROW_Y (PANEL_Y + 26)

static Rectangle weaponButtonRect(int i) { return (Rectangle) { 30, (float)(ROW_Y + i * 34), 355, 30 }; }
static Rectangle hackButtonRect(void) { return (Rectangle) { 470, PANEL_Y + 4, 120, 18 }; }
static Rectangle endTurnButtonRect(void) { return (Rectangle) { 600, PANEL_Y + 4, 170, 18 }; }
static Rectangle logStripRect(void) { return (Rectangle) { 20, PANEL_Y - 22, SCREEN_W - 40, 20 }; }
#define HEAT_CRITICAL 0.75f

void uiBattleOpen(void) {
    weaponSel = 0;
    reselectAfterShot = 0;
    logOpen = infoOpen = 0;
    lastPhase = -1;
    heatWasCritical = 0;
    lastSeenLog = NULL;
    clearEffects();
}

// Sounds for things that happen inside the battle logic
static void battleSounds(void) {
    if ((int)battle.phase != lastPhase) {
        if (battle.phase == BP_VICTORY) sfxPlay(battle.hacked ? SFX_HACK : SFX_VICTORY);
        if (battle.phase == BP_DEFEAT) sfxPlay(SFX_DEFEAT);
        lastPhase = battle.phase;
    }
    const LogEntry* newest = battleLogEntry(0);
    if (newest && newest != lastSeenLog) {
        if (strstr(newest->text, "SCRAMBLED") || strstr(newest->text, "CORRUPT")) sfxPlay(SFX_SCRAMBLE);
        lastSeenLog = newest;
    }
    const MechStats* s = &battleFieldPlayer()->mech->stats;
    int critical = s->maxHeat > 0 && s->heat >= s->maxHeat * HEAT_CRITICAL;
    if (critical && !heatWasCritical) sfxPlay(SFX_HEAT_WARN);
    heatWasCritical = critical;
}

// The battle log overlay takes over input while open
static void updateLogOverlay(void) {
    int n = battleLogCount();
    if (IsKeyPressed(KEY_L) || IsKeyPressed(KEY_ESCAPE) || clickedOn(logStripRect())) { consumeInput(); logOpen = 0; return; }
    if (n == 0) return;
    if (DOWN_PRESSED) logSel = logSel + 1 < n ? logSel + 1 : logSel;
    if (UP_PRESSED) logSel = logSel > 0 ? logSel - 1 : 0;
    float wheel = GetMouseWheelMove();
    if (wheel < 0 && logSel + 1 < n) logSel++;
    if (wheel > 0 && logSel > 0) logSel--;
}

static void selectFireableWeapon(void) {
    if (battleCanFire(weaponSel, NULL)) return;
    for (int i = 1; i < MAX_WEAPONS; i++) {
        int idx = (weaponSel + i) % MAX_WEAPONS;
        if (battleCanFire(idx, NULL)) { weaponSel = idx; return; }
    }
}

void uiBattleUpdate(float dt, GameState* state) {
    battleUpdate(dt);
    updateEffects(dt);
    BattleEvent ev;
    while (battlePopEvent(&ev)) {
        Vector2 from = ev.fromPlayer ? playerMechPos : enemyMechPos;
        Vector2 to = ev.fromPlayer ? enemyMechPos : playerMechPos;
        addEffect(&ev, from, to);
        if (ev.munition >= 0) sfxFire(ev.munition);
        else if (ev.fx == FX_SCAN) sfxPlay(SFX_HACK);
    }
    battleSounds();

    if (logOpen) { updateLogOverlay(); return; }
    if (IsKeyPressed(KEY_L) || clickedOn(logStripRect())) { consumeInput(); logOpen = 1; logSel = 0; return; }
    if (IsKeyPressed(KEY_I)) infoOpen = !infoOpen;
    if (IsKeyPressed(KEY_V)) formulaView = !formulaView;

    if (battle.testRange) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            consumeInput();
            battleEndTestRange();
            *state = STATE_MENU;
            return;
        }
        if (IsKeyPressed(KEY_R) && !battleBusy()) {
            battleResetDummy();
            snprintf(battle.log, sizeof(battle.log), "TARGET DUMMY rebuilt.");
        }
    }

    if (battle.phase == BP_OVER) {
        *state = battle.result == RESULT_TO_REVISION ? STATE_REVISION : STATE_OVERWORLD;
        return;
    }

    int cont = confirmPressed() || clickPressed();
    if (battle.dialogue != DLG_NONE || battle.phase == BP_VICTORY || battle.phase == BP_DEFEAT) {
        if (cont && !(battle.dialogue == DLG_NONE && battleBusy())) {
            consumeInput();
            battleConfirm();
        }
        return;
    }
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) return;

    if (reselectAfterShot) { reselectAfterShot = 0; selectFireableWeapon(); }

    if (DOWN_PRESSED || RIGHT_PRESSED) { weaponSel = (weaponSel + 1) % MAX_WEAPONS; sfxPlay(SFX_UI_MOVE); }
    if (UP_PRESSED || LEFT_PRESSED)    { weaponSel = (weaponSel + MAX_WEAPONS - 1) % MAX_WEAPONS; sfxPlay(SFX_UI_MOVE); }

    // Mouse: hovering selects a weapon (and shows its preview), clicking fires it
    int clickedWeapon = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) {
        if (mouseMoved() && mouseOver(weaponButtonRect(i))) weaponSel = i;
        if (clickedOn(weaponButtonRect(i))) { weaponSel = i; clickedWeapon = 1; }
    }

    if ((IsKeyPressed(KEY_C) || clickedOn(hackButtonRect())) && battleCanHack()) {
        consumeInput();
        battleHack();
        return;
    }
    if (IsKeyPressed(KEY_X) || clickedOn(endTurnButtonRect())) {
        consumeInput();
        battleEndTurn();
        return;
    }
    if (confirmPressed() || clickedWeapon) {
        consumeInput();
        const char* reason = NULL;
        if (battleCanFire(weaponSel, &reason)) {
            battleFire(weaponSel);
            reselectAfterShot = 1;
        }
        else {
            const Weapon* w = mechWeapon(battleFieldPlayer()->mech, weaponSel);
            snprintf(battle.log, sizeof(battle.log), "%s: %s", w ? w->name : "MOUNT", reason);
            sfxPlay(SFX_UI_DENY);
        }
    }
}

static void drawStarfield(int count, float speed) {
    for (int i = 0; i < count; i++) {
        int stx = (i * 137 + (int)(glowTimer * speed)) % screenW;
        int sty = (i * 91) % SCREEN_H;
        int bright = 40 + (int)(30 * (sinf((glowTimer * 30 + i) * 0.1f) * 0.5f + 0.5f));
        DrawPixel(stx, sty, (Color) { (unsigned char)bright, (unsigned char)bright, (unsigned char)(bright + 40), 255 });
    }
}

// Pip states: 0 = empty, 1 = charged, 2 = charged but spent by the previewed shot
static void drawEnergyPip(int cx, int cy, int r, int state, int i) {
    float blink = 0.5f + 0.5f * sinf(glowTimer * 8);
    Color fill = state == 1 ? (Color) { 60, 200, 255, 255 }
        : state == 2 ? (Color) { 60, 200, 255, (unsigned char)(90 + 120 * blink) } : (Color) { 40, 45, 60, 255 };
    Color edge = state ? (Color) { 180, 240, 255, 255 } : (Color) { 70, 80, 100, 255 };
    if (state == 1) {
        float p = 0.5f + 0.5f * sinf(glowTimer * 5 + i);
        DrawCircle(cx, cy, r + p * 3, (Color) { 60, 180, 255, 40 });
    }
    DrawCircle(cx, cy, (float)r, fill);
    DrawCircleLines(cx, cy, (float)r, edge);
    DrawCircle(cx - r / 3, cy - r / 3, r / 4.0f, (Color) { 255, 255, 255, (unsigned char)(state ? 220 : 40) });
}

// Blinking segment over a bar showing how much the previewed attack would remove
static void drawLossGhost(int x, int y, int w, int h, int value, int loss, int max) {
    if (loss <= 0 || max <= 0) return;
    if (loss > value) loss = value;
    float blink = 0.5f + 0.5f * sinf(glowTimer * 8);
    int x0 = x + 1 + (int)((w - 2) * (float)(value - loss) / max);
    int x1 = x + 1 + (int)((w - 2) * (float)value / max);
    DrawRectangle(x0, y + 1, x1 - x0, h - 2, (Color) { 255, 255, 255, (unsigned char)(80 + 120 * blink) });
}

// Heat gauge with 25% ticks and a blinking preview of the next shot's heat
static void drawHeatGauge(int x, int y, int w, int h, int heat, int maxHeat, int addHeat) {
    drawHeatBar(x, y, w, h, heat, maxHeat);
    if (addHeat > 0 && maxHeat > 0) {
        float blink = 0.5f + 0.5f * sinf(glowTimer * 8);
        int after = heat + addHeat;
        int x0 = x + 1 + (int)((w - 2) * fminf(1, (float)heat / maxHeat));
        int x1 = x + 1 + (int)((w - 2) * fminf(1, (float)after / maxHeat));
        Color c = after > maxHeat ? (Color) { 255, 60, 60, 255 } : (Color) { 255, 200, 120, 255 };
        c.a = (unsigned char)(90 + 120 * blink);
        DrawRectangle(x0, y + 1, x1 - x0, h - 2, c);
    }
    for (int q = 1; q < 4; q++) DrawLine(x + w * q / 4, y, x + w * q / 4, y + h, (Color) { 60, 30, 20, 255 });
}

// Effective values; red/green when battle modifiers move them off the base stat
static void drawReadouts(const MechStats* s, int mobility, int accuracy, int x, int y) {
    Color base = { 170, 200, 230, 255 }, down = { 255, 130, 120, 255 }, up = { 120, 255, 160, 255 };
    DrawText(TextFormat("ACC %d%%", accuracy), x, y, 12, accuracy < s->accuracy ? down : accuracy > s->accuracy ? up : base);
    DrawText(TextFormat("MOB %d%%", mobility), x + 72, y, 12, mobility > s->mobility ? up : base);
    DrawText(TextFormat("STB %d%%", s->stability), x + 144, y, 12, base);
    DrawText(TextFormat("PWR %.2fx", s->power), x + 216, y, 12, base);
    tipText((Rectangle) { (float)x, (float)y, 68, 14 }, TextFormat("ACCURACY %d%%", accuracy),
        TextFormat("Multiplies the hit chance of every weapon this machine fires. Base %d%%; firmware (Precision Strike, "
            "Recursive Targeting) raises it for this turn, scrambles lower it.", s->accuracy));
    tipText((Rectangle) { (float)x + 72, (float)y, 68, 14 }, TextFormat("MOBILITY %d%%", mobility),
        TextFormat("Chance to dodge: every attack against it hits %d%% less often (Mobility / 2). Evasive firmware "
            "adds to it until its next turn.", mobility / 2));
    tipText((Rectangle) { (float)x + 144, (float)y, 68, 14 }, TextFormat("STABILITY %d%%", s->stability),
        "Resistance to scrambles, corruption and hacking. A scramble of strength S lands with chance S / (S + Stability).");
    tipText((Rectangle) { (float)x + 216, (float)y, 76, 14 }, TextFormat("POWER %.2fx", s->power),
        "Multiplies the base damage of every weapon this machine fires.");
}

static int previewSelected(AttackPreview* p) {
    return battle.phase == BP_PLAYER_TURN && battle.dialogue == DLG_NONE && battlePreviewPlayer(weaponSel, p);
}

static void drawEnemyHud(const AttackPreview* p) {
    const Mech* m = battleFieldEnemy()->mech;
    const MechStats* s = &m->stats;
    const MechModel* model = mechModel(m);
    DrawRectangle(20, 20, 300, 132, (Color) { 20, 25, 40, 230 });
    if (battle.testRange) {
        DrawRectangleLines(20, 20, 300, 132, (Color) { 230, 200, 90, 220 });
        DrawText("TEST RANGE", 28, 24, 12, (Color) { 255, 220, 100, 255 });
        DrawText(TextFormat("DESTROYED %d", battle.dummyKills), 220, 24, 12, (Color) { 255, 220, 100, 255 });
    }
    else if (battle.trainer >= 0) {
        Trainer* t = &trainers[battle.trainer];
        DrawRectangleLines(20, 20, 300, 132, (Color) { 255, 200, 60, 220 });
        DrawText(TextFormat("%s  %s", factions[t->faction].name, t->name), 28, 24, 12, factions[t->faction].color);
        DrawText(TextFormat("MECH %d/%d", t->numDefeated + 1, t->numMechs), 250, 24, 12, (Color) { 255, 200, 100, 255 });
    }
    else {
        DrawRectangleLines(20, 20, 300, 132, (Color) { 255, 80, 80, 220 });
        DrawText(TextFormat("ROGUE AI  %s", factions[FAC_WILD].name), 28, 24, 12, (Color) { 255, 100, 100, 255 });
    }
    if (battleCanHack())
        DrawText(TextFormat("HACK %d%%", (int)roundf(battleHackChance() * 100)), 240, 24, 12, (Color) { 120, 255, 220, 255 });
    DrawText(m->name, 30, 38, 22, (Color) { 255, 210, 210, 255 });
    DrawText(TextFormat("FW %s", firmwareLabel(m->fw.revision)), 250, 40, 18, WHITE);
    const char* arch = battleFieldEnemy()->archetype >= 0 ? TextFormat("  [%s%s]", archetypes[battleFieldEnemy()->archetype].boss ? "BOSS " : "",
        archetypes[battleFieldEnemy()->archetype].name) : "";
    DrawText(TextFormat("%s %s - %s%s", model->designation, model->name, roleName(mechRole(m)), arch),
        30, 60, 11, (Color) { 200, 200, 240, 255 });
    drawIntegrityBar(30, 76, 280, 10, s->integrity, s->maxIntegrity);
    drawArmorBar(30, 102, 280, 8, s->armor, s->maxArmor);
    if (p) {
        drawLossGhost(30, 76, 280, 10, s->integrity, p->integrityDamage, s->maxIntegrity);
        drawLossGhost(30, 102, 280, 8, s->armor, p->armorDamage, s->maxArmor);
    }
    DrawText(TextFormat("INTEGRITY %d/%d", s->integrity, s->maxIntegrity), 30, 88, 11, WHITE);
    DrawText(TextFormat("ARMOR %d/%d", s->armor, s->maxArmor), 30, 112, 11, (Color) { 150, 190, 240, 255 });
    drawReadouts(s, combatMobility(battleFieldEnemy()), combatAccuracy(battleFieldEnemy()), 30, 132);
    tipText((Rectangle) { 30, 74, 280, 26 }, TextFormat("INTEGRITY %d/%d", s->integrity, s->maxIntegrity),
        "The machine's health. At 0 it is scrapped. Penetrating damage and anything Armor can't absorb lands here.");
    tipText((Rectangle) { 30, 100, 280, 24 }, TextFormat("ARMOR %d/%d", s->armor, s->maxArmor),
        "A second pool on top of Integrity. Each hit splits: the weapon's penetration % goes straight to Integrity, "
        "the rest is soaked by Armor until it runs out, then spills over. Armor Analysis and Siege add penetration.");
}

static void drawPlayerHud(const AttackPreview* p) {
    const Combatant* c = battleFieldPlayer();
    const Mech* m = c->mech;
    const MechStats* s = &m->stats;
    const MechModel* model = mechModel(m);
    int x = 400, y = 214;
    DrawRectangle(x, y, 380, 176, (Color) { 20, 30, 50, 230 });
    DrawRectangleLines(x, y, 380, 176, model->accent);
    DrawText("ALLIED", x + 8, y + 4, 12, (Color) { 100, 240, 255, 255 });
    DrawText(m->name, x + 10, y + 18, 22, model->accent);
    DrawText(TextFormat("FW %s", firmwareLabel(m->fw.revision)), x + 300, y + 20, 18, WHITE);
    drawIntegrityBar(x + 10, y + 46, 360, 10, s->integrity, s->maxIntegrity);
    DrawText(TextFormat("INTEGRITY %d/%d", s->integrity, s->maxIntegrity), x + 10, y + 58, 11, WHITE);
    drawArmorBar(x + 10, y + 72, 360, 8, s->armor, s->maxArmor);
    DrawText(TextFormat("ARMOR %d/%d", s->armor, s->maxArmor), x + 10, y + 82, 11, (Color) { 150, 190, 240, 255 });
    drawHeatGauge(x + 10, y + 98, 360, 10, s->heat, s->maxHeat, p ? p->heat : 0);
    DrawText(TextFormat("HEAT %d/%d%s   COOLING -%d/turn", s->heat, s->maxHeat, p ? TextFormat(" (+%d)", p->heat) : "", s->cooling),
        x + 10, y + 110, 11, (Color) { 255, 170, 100, 255 });
    float heatFrac = s->maxHeat > 0 ? (float)s->heat / s->maxHeat : 0;
    if (heatFrac >= HEAT_CRITICAL) {
        float blink = 0.5f + 0.5f * sinf(glowTimer * 10);
        DrawText("HEAT CRITICAL", x + 250, y + 110, 12, (Color) { 255, 70, 60, (unsigned char)(140 + 115 * blink) });
    }
    else if (heatFrac >= 0.5f) DrawText("RUNNING HOT", x + 270, y + 110, 11, (Color) { 255, 170, 80, 255 });
    tipText((Rectangle) { (float)x + 10, (float)y + 96, 360, 26 }, TextFormat("HEAT %d / %d", s->heat, s->maxHeat),
        TextFormat("Every shot adds heat. A weapon that would push heat past %d can't fire (THERMAL LIMIT). At the start of "
            "your turn %d heat vents. Above 75%% you are one or two shots from locking your big weapons.", s->maxHeat, s->cooling));
    drawReadouts(s, combatMobility(c), combatAccuracy(c), x + 10, y + 128);
    int need = firmwareDataToNext(m->fw.revision);
    tipText((Rectangle) { (float)x + 10, (float)y + 44, 360, 26 }, TextFormat("INTEGRITY %d/%d", s->integrity, s->maxIntegrity),
        "Your health. At 0 your mech is disabled: the battle is lost and a recovery fee is charged.");
    tipText((Rectangle) { (float)x + 10, (float)y + 70, 360, 24 }, TextFormat("ARMOR %d/%d", s->armor, s->maxArmor),
        "Soaks the non-penetrating part of every hit before Integrity. Replated for free after each battle.");
    drawDataBar(x + 10, y + 150, 250, 5, m->fw.data, need);
    DrawText(TextFormat("DATA %d/%d", m->fw.data, need), x + 268, y + 147, 10, (Color) { 200, 170, 255, 255 });
    if (c->accPenalty > 0 || c->disabledWeapon >= 0)
        DrawText(TextFormat("SCRAMBLED%s%s", c->accPenalty > 0 ? TextFormat(" ACC-%d", c->accPenalty) : "",
            c->disabledWeapon >= 0 ? " WPN OFFLINE" : ""), x + 10, y + 160, 10, (Color) { 200, 150, 255, 255 });
}

static void drawReactor(const AttackPreview* p) {
    const MechStats* s = &battleFieldPlayer()->mech->stats;
    int pips = s->maxEnergy > s->energy ? s->maxEnergy : s->energy;
    int spend = p ? p->energyCost : 0;
    DrawRectangle(330, 20, 200, 80, (Color) { 15, 25, 45, 230 });
    DrawRectangleLines(330, 20, 200, 80, (Color) { 100, 200, 255, 220 });
    DrawText(TextFormat("REACTOR  %d/%d EN", s->energy, s->maxEnergy), 362, 25, 14, (Color) { 150, 220, 255, 255 });
    int spacing = pips > 4 ? 30 : 40;
    int r = pips > 4 ? 11 : 14;
    int x0 = 430 - (pips - 1) * spacing / 2;
    for (int i = 0; i < pips; i++) {
        int state = i >= s->energy ? 0 : (i >= s->energy - spend ? 2 : 1);
        drawEnergyPip(x0 + i * spacing, 64, r, state, i);
    }
    DrawText(TextFormat("ROUND %d", battle.round), 340, 106, 14, (Color) { 150, 220, 255, 255 });
    tipText((Rectangle) { 330, 20, 200, 80 }, TextFormat("ENERGY %d/%d", s->energy, s->maxEnergy),
        "Actions this turn. Each weapon costs its pips (shown on the weapon). Energy refills at the start of your turn; "
        "blinking pips are what the selected weapon would spend.");
    if (battleFieldPlayer()->actionsThisTurn == 0 && firmwareEffect(&battleFieldPlayer()->mech->fw, CFX_FIRST_ACTION_FREE) > 0)
        DrawText("FIRST ACTION FREE", 420, 108, 10, (Color) { 120, 255, 180, 255 });
    // Active Firmware Corruption on the player
    const Firmware* fw = &battleFieldPlayer()->mech->fw;
    int offline = 0, reversed = 0;
    for (int k = 0; k < MAX_SOCKETS; k++) {
        if (fw->chips[k] < 0 || fw->corrupt[k] <= 0) continue;
        if (fw->corruptKind[k] == CORRUPT_REVERSED) reversed++; else offline++;
    }
    const char* corrupt = "";
    if (offline) corrupt = TextFormat("%s%d OFFLINE ", corrupt, offline);
    if (reversed) corrupt = TextFormat("%s%d REVERSED ", corrupt, reversed);
    if (battleFieldPlayer()->energyTax) corrupt = TextFormat("%sEN COST +%d ", corrupt, battleFieldPlayer()->energyTax);
    if (battleFieldPlayer()->randomTargeting) corrupt = TextFormat("%sRANDOM TARGETING", corrupt);
    if (corrupt[0]) {
        DrawText(TextFormat("CORRUPTED: %s", corrupt), 340, 122, 10, (Color) { 255, 110, 90, 255 });
        tipText((Rectangle) { 340, 120, 300, 14 }, "FIRMWARE CORRUPTION",
            "OFFLINE chips do nothing; REVERSED chips do the opposite (+5 Accuracy becomes -5). EN COST: every weapon costs "
            "1 more Energy. RANDOM TARGETING: each shot has a 50% chance to fire a random weapon. It wears off after your next turn.");
    }
}

static void drawWeaponButton(int i) {
    Rectangle r = weaponButtonRect(i);
    int bx = (int)r.x, by = (int)r.y;
    const Weapon* w = mechWeapon(battleFieldPlayer()->mech, i);
    int selected = (i == weaponSel);
    if (!w) {
        DrawRectangleLinesEx(r, 1, selected ? (Color) { 120, 120, 120, 255 } : (Color) { 50, 70, 100, 200 });
        DrawText("-- EMPTY MOUNT --", bx + 26, by + 9, 12, (Color) { 90, 100, 120, 255 });
        return;
    }
    const char* reason = NULL;
    int usable = battleCanFire(i, &reason) || (reason && strcmp(reason, "STANDBY") == 0);
    Color col = munitionColor(w->munition);
    Color border = selected ? (usable ? col : (Color) { 120, 120, 120, 255 }) : (Color) { 60, 120, 180, 200 };
    if (selected) DrawRectangleRec(r, (Color) { col.r, col.g, col.b, (unsigned char)(usable ? 60 : 25) });
    DrawRectangleLinesEx(r, selected ? 2.0f : 1.0f, border);
    DrawCircle(bx + 12, by + 10, 5, usable ? col : (Color) { 90, 90, 100, 255 });
    DrawText(w->name, bx + 24, by + 3, 14, usable ? (Color) { 220, 240, 255, 255 } : (Color) { 110, 120, 140, 255 });

    AttackPreview p;
    battlePreviewPlayer(i, &p);
    // Armor penetration badge and the on-hit split (red = Integrity, blue = Armor)
    if (w->baseDamage > 0) {
        const char* tag;
        Color tc;
        if (p.armorBefore <= 0) { tag = "NO ARMOR"; tc = (Color) { 255, 120, 110, 255 }; }
        else if (p.pen >= 50) { tag = TextFormat("PIERCE %d%%", p.pen); tc = (Color) { 120, 255, 160, 255 }; }
        else if (p.integrityDamage * 3 < p.armorDamage) { tag = TextFormat("BLOCKED %d%%", p.pen); tc = (Color) { 255, 150, 110, 255 }; }
        else { tag = TextFormat("PEN %d%%", p.pen); tc = (Color) { 255, 220, 120, 255 }; }
        DrawText(tag, bx + 200, by + 3, 10, usable ? tc : (Color) { 110, 120, 140, 255 });
        int total = p.armorDamage + p.integrityDamage;
        if (total > 0) {
            int bw = 70, iw = bw * p.integrityDamage / total;
            DrawRectangle(bx + 200, by + 14, iw, 3, (Color) { 255, 90, 90, 255 });
            DrawRectangle(bx + 200 + iw, by + 14, bw - iw, 3, (Color) { 120, 190, 255, 255 });
        }
    }
    const char* info = w->baseDamage > 0
        ? TextFormat("HIT %d%%  ARM-%d INT-%d  HEAT+%d", (int)roundf(p.hitChance * 100), p.armorDamage, p.integrityDamage, p.heat)
        : TextFormat("HIT %d%%  SCRAMBLE %d  HEAT+%d", (int)roundf(p.hitChance * 100), p.scramble, p.heat);
    if (!usable && reason) info = reason;
    DrawText(info, bx + 24, by + 18, 10, usable ? (Color) { 150, 200, 220, 255 } : (Color) { 255, 120, 120, 255 });
    if (w->ammo > 0)
        DrawText(TextFormat("AMMO %d/%d", battleFieldPlayer()->mech->weapons[i].ammo, w->ammo), bx + 225, by + 18, 10,
            (Color) { 150, 200, 220, 255 });
    for (int e = 0; e < w->energyCost; e++) {
        int ex = bx + (int)r.width - 14 - e * 14, ey = by + 10;
        DrawCircle(ex, ey, 5, usable ? (Color) { 60, 200, 255, 255 } : (Color) { 70, 80, 100, 255 });
        DrawCircleLines(ex, ey, 5, (Color) { 180, 240, 255, 255 });
    }
    Explanation why;
    if (battleExplainPlayer(i, &why))
        tipWhy(r, w->name, TextFormat("%s  Mount rating %d/5.", munitionPlain(w->munition), battleFieldPlayer()->mech->weapons[i].rating), &why);
}

// Plain-language summary of the selected weapon: what it will do and what to watch out for
static void drawPlainPreview(const AttackPreview* p, const Weapon* w, int x, int y) {
    Color txt = { 210, 225, 240, 255 }, good = { 120, 255, 160, 255 }, warn = { 255, 150, 110, 255 };
    const MechStats* ds = &battleFieldEnemy()->mech->stats;
    int hitPct = (int)roundf(p->hitChance * 100);
    DrawText(TextFormat("%s > %s", w->name, battleFieldEnemy()->mech->name), x, y, 12, munitionColor(w->munition));
    int ly = y + 17;
    DrawText(TextFormat("%d%% TO HIT", hitPct), x, ly, 14, hitPct >= 70 ? good : hitPct >= 40 ? (Color) { 255, 220, 120, 255 } : warn);
    if (w->baseDamage > 0) {
        DrawText(TextFormat("ON HIT  ARMOR -%d  INTEGRITY -%d", p->armorDamage, p->integrityDamage), x + 110, ly + 2, 11, txt);
        ly += 18;
        const char* pen = p->armorBefore <= 0 ? "No Armor left: the full hit lands on Integrity."
            : p->pen >= 50 ? TextFormat("Armor-piercing: %d%% goes straight through their %d Armor.", p->pen, p->armorBefore)
            : p->integrityDamage * 3 < p->armorDamage ? TextFormat("Mostly blocked: their %d Armor soaks all but %d%%.", p->armorBefore, p->pen)
            : TextFormat("%d%% penetrates; their Armor soaks the rest.", p->pen);
        DrawText(pen, x, ly, 10, p->armorBefore > 0 && p->integrityDamage * 3 < p->armorDamage ? warn : txt);
        ly += 13;
        if (p->integrityDamage >= ds->integrity) { DrawText("A hit here SCRAPS the target.", x, ly, 10, good); ly += 13; }
        else { DrawText(TextFormat("Expected %.0f damage per shot (misses included).", (p->armorDamage + p->integrityDamage) * p->hitChance), x, ly, 10, txt); ly += 13; }
    }
    else ly += 18;
    if (p->scramble > 0) {
        DrawText(TextFormat("Scramble lands %d%% of the time (their Stability resists).", (int)roundf((1 - p->resist) * 100)),
            x, ly, 10, (Color) { 200, 170, 255, 255 });
        ly += 13;
    }
    int after = p->heatBefore + p->heat;
    if (after > p->maxHeat) DrawText(TextFormat("OVER HEAT LIMIT: %d/%d - cannot fire.", after, p->maxHeat), x, ly, 10, warn);
    else {
        int blocked[MAX_WEAPONS];
        int nb = battleHeatBlocksNextTurn(weaponSel, blocked, MAX_WEAPONS);
        if (nb > 0) {
            char names[96] = "";
            for (int k = 0; k < nb; k++)
                snprintf(names + strlen(names), sizeof(names) - strlen(names), "%s%s", k ? ", " : "",
                    mechWeapon(battleFieldPlayer()->mech, blocked[k])->name);
            DrawText(TextFormat("Heat %d/%d. Locks %s next turn.", after, p->maxHeat, names), x, ly, 10, warn);
        }
        else DrawText(TextFormat("Heat %d/%d after this shot - safe.", after, p->maxHeat), x, ly, 10,
            after >= p->maxHeat * HEAT_CRITICAL ? warn : good);
    }
    ly += 13;
    DrawText(TextFormat("Costs %d EN (%d left).", p->energyCost, p->energyBefore - p->energyCost), x, ly, 10, txt);
    DrawText("[I] why   [V] formula   [L] log   hover anything", x, y + 122, 10, (Color) { 100, 200, 240, 255 });
}

// Every step of the attack formula for the selected weapon, before it is fired
static void drawPreviewPanel(const AttackPreview* p, const Weapon* w, int x, int y) {
    Color txt = { 210, 225, 240, 255 }, dim = { 130, 150, 175, 255 }, res = { 120, 255, 160, 255 };
    DrawText(TextFormat("PREVIEW  %s > %s", w->name, battleFieldEnemy()->mech->name), x, y, 12, munitionColor(w->munition));
    int ly = y + 16, lh = 12;
    DrawText(TextFormat("HIT  %d%% x %d/100 x (1 - %d/200) = %.1f%%%s", p->weaponAcc, p->accuracy, p->mobility,
        p->hitUnclamped * 100, p->spoofMod < 1 ? TextFormat(" x spoof %.2f", p->spoofMod) : ""), x, ly, 10, txt);
    DrawText(TextFormat("     clamp 5-95%%  ->  %.1f%% to hit", p->hitChance * 100), x, ly += lh, 10, dim);
    DrawText(TextFormat("RAW  %d x PWR %.2f = %.1f", p->baseDamage, p->power, p->raw), x, ly += lh, 10, txt);
    DrawText(TextFormat("PEN %d%%  INT %.1f x %.2f = %.1f  ARM %.1f x %.2f = %.1f", p->pen, p->raw, p->pen / 100.0f,
        p->split.toIntegrity, p->raw, 1 - p->pen / 100.0f, p->split.toArmor), x, ly += lh, 10, txt);
    DrawText(TextFormat("ARMOR %d absorbs %d%s, spill %d -> INT", p->armorBefore, p->split.armorDamage,
        p->breachBonus > 0 ? TextFormat(" (+%d breach)", p->breachBonus) : "", p->split.spill), x, ly += lh, 10, txt);
    DrawText(TextFormat("ON HIT  ARM -%d   INT -%d   (expected %.1f)", p->armorDamage, p->integrityDamage,
        (p->armorDamage + p->integrityDamage) * p->hitChance), x, ly += lh, 10, res);
    if (p->scramble > 0)
        DrawText(TextFormat("SCRAMBLE %d: resist = STB / (STB + %d) = %.1f%%", p->scramble, p->scramble, p->resist * 100),
            x, ly += lh, 10, (Color) { 200, 170, 255, 255 });
    int heatAfter = p->heatBefore + p->heat;
    DrawText(TextFormat("EN %d -> %d    HEAT %d + %d = %d/%d%s", p->energyBefore, p->energyBefore - p->energyCost,
        p->heatBefore, p->heat, heatAfter, p->maxHeat, heatAfter > p->maxHeat ? " OVER LIMIT" : ""),
        x, ly += lh, 10, heatAfter > p->maxHeat ? (Color) { 255, 120, 110, 255 } : (Color) { 255, 170, 100, 255 });
}

// Scrollable history; the selected entry expands into its full breakdown
static void drawLogOverlay(void) {
    int x = 20, y = 58, w = SCREEN_W - 40, h = PANEL_Y - 24 - 58;
    DrawRectangle(x, y, w, h, (Color) { 6, 10, 22, 255 });
    DrawRectangleLines(x, y, w, h, (Color) { 120, 220, 255, 230 });
    DrawText("BATTLE LOG", x + 10, y + 6, 16, (Color) { 150, 230, 255, 255 });
    DrawText("[W/S] select   [L/ESC] close   newest first", x + 150, y + 9, 10, (Color) { 120, 160, 200, 255 });
    int n = battleLogCount();
    const int rows = 10, rowH = 14;
    int first = logSel >= rows ? logSel - rows + 1 : 0;
    for (int r = 0; r < rows && first + r < n; r++) {
        const LogEntry* e = battleLogEntry(first + r);
        int ry = y + 28 + r * rowH, sel = first + r == logSel;
        if (sel) DrawRectangle(x + 4, ry - 1, w - 8, rowH, (Color) { 40, 80, 120, 200 });
        Color side = e->side == 1 ? (Color) { 100, 230, 255, 255 } : e->side == 0 ? (Color) { 255, 120, 110, 255 } : (Color) { 150, 160, 180, 255 };
        DrawText(TextFormat("R%d", e->round), x + 8, ry, 10, (Color) { 120, 140, 170, 255 });
        if (e->munition >= 0) DrawCircle(x + 38, ry + 5, 4, munitionColor(e->munition));
        char line[120];
        snprintf(line, sizeof(line), "%s", e->text);
        if (MeasureText(line, 10) > w - 70) {
            while (strlen(line) > 4 && MeasureText(line, 10) > w - 90) line[strlen(line) - 1] = 0;
            strcat(line, "...");
        }
        DrawText(line, x + 48, ry, 10, side);
    }
    int dy = y + 28 + rows * rowH + 6;
    DrawLine(x + 6, dy - 3, x + w - 6, dy - 3, (Color) { 60, 100, 150, 200 });
    const LogEntry* e = battleLogEntry(logSel);
    if (!e) return;
    if (e->why.n == 0) { DrawText("No breakdown for system messages.", x + 10, dy + 2, 10, (Color) { 130, 150, 175, 255 }); return; }
    for (int i = 0; i < e->why.n && dy + i * 12 < y + h - 10; i++)
        DrawText(e->why.line[i], x + 10, dy + i * 12, 10, e->why.warn[i] ? (Color) { 255, 150, 110, 255 } : (Color) { 190, 225, 200, 255 });
}

void uiBattleDraw(void) {
    float sx = 0, sy = 0;
    if (shakeTimer > 0) {
        sx = (float)GetRandomValue(-100, 100) / 100.0f * shakeAmount;
        sy = (float)GetRandomValue(-100, 100) / 100.0f * shakeAmount;
    }
    ClearBackground((Color) { 10, 12, 22, 255 });
    drawStarfield(60 * screenW / SCREEN_W, 0);
    for (int i = 0; i < 20; i++)
        DrawLine(0, 180 + i * 12, screenW, 180 + i * 12, (Color) { 30, 60, 100, (unsigned char)(80 - i * 3) });
    int fanLines = screenW / 80 + 1;
    for (int i = -fanLines; i <= fanLines; i++) {
        int x = screenW / 2 + i * 40;
        DrawLine(x, 180, x + i * 30, SCREEN_H, (Color) { 30, 60, 100, 60 });
    }

    AttackPreview preview;
    const AttackPreview* p = previewSelected(&preview) && !battleBusy() ? &preview : NULL;
    tip.active = 0;

    BeginMode2D(layoutCamera());
    drawMechBattle(battleFieldEnemy()->mech->model, (int)(enemyMechPos.x + sx), (int)(enemyMechPos.y + sy), 14, 1);
    drawMechBattle(battleFieldPlayer()->mech->model, (int)(playerMechPos.x + sx), (int)(playerMechPos.y + sy), 14, 0);
    drawEffects();
    drawParticles();
    drawDamageNums();

    drawEnemyHud(p);
    drawPlayerHud(p);
    drawReactor(p);

    // Log strip and bottom panel
    DrawRectangle(20, PANEL_Y - 22, SCREEN_W - 40, 20, (Color) { 10, 15, 30, 200 });
    DrawText(battle.log, 30, PANEL_Y - 18, 12, (Color) { 190, 220, 240, 255 });
    DrawText("[L] LOG", SCREEN_W - 70, PANEL_Y - 17, 10, (Color) { 100, 200, 240, 255 });
    const LogEntry* latest = battleLogEntry(0);
    if (latest && latest->why.n > 0) tipWhy(logStripRect(), "LAST ACTION", latest->text, &latest->why);
    DrawRectangle(20, PANEL_Y, SCREEN_W - 40, 172, (Color) { 15, 20, 35, 240 });
    DrawRectangleLines(20, PANEL_Y, SCREEN_W - 40, 172, (Color) { 80, 220, 255, 200 });

    if (battle.phase == BP_PLAYER_TURN && battle.dialogue == DLG_NONE) {
        DrawText(">> SELECT WEAPON <<", 30, PANEL_Y + 6, 14, (Color) { 100, 240, 255, 255 });
        for (int i = 0; i < MAX_WEAPONS; i++) drawWeaponButton(i);
        drawButton(hackButtonRect(), "HACK [C]", 12, 0, battleCanHack());
        drawButton(endTurnButtonRect(), "END TURN [X]", 12, 0, 1);
        DrawLine(393, ROW_Y, 393, ROW_Y + 134, (Color) { 50, 90, 130, 200 });
        if (p && formulaView) drawPreviewPanel(p, mechWeapon(battleFieldPlayer()->mech, weaponSel), 402, ROW_Y);
        else if (p) drawPlainPreview(p, mechWeapon(battleFieldPlayer()->mech, weaponSel), 402, ROW_Y);
        else if (!battleBusy()) DrawText("No weapon on this mount.", 402, ROW_Y, 12, (Color) { 130, 150, 175, 255 });
        DrawText(battle.testRange ? "[Z] FIRE  [W/S] SELECT  [X] END TURN  [R] NEW DUMMY  [L] LOG  [M] MUTE  [ESC] LEAVE"
                                  : "[Z] FIRE  [W/S] SELECT  [X] END TURN  [C] HACK  [L] LOG  [I] WHY  [M] MUTE",
            30, ROW_Y + 134, 10, (Color) { 100, 240, 255, 255 });
        tipText(hackButtonRect(), "HACK (REPROGRAM)", battleCanHack()
            ? TextFormat("Chance %d%% = your hack Strength %d / (Strength + their Stability %d). Scramble them first: every "
                "pending scramble or corruption lowers their Stability by %d. Costs the rest of your turn.",
                (int)roundf(battleHackChance() * 100), hackStrength(battleFieldPlayer()->mech), battleHackStability(), HACK_STABILITY_PER_EFFECT)
            : "Only unmanned rogue AI machines can be reprogrammed, and your team needs a free slot.");
    }
    else {
        DrawText(">> SYS LOG <<", 30, PANEL_Y + 6, 14, (Color) { 100, 240, 255, 255 });
        DrawText(battle.log, 35, PANEL_Y + 34, 18, (Color) { 220, 240, 255, 255 });

        if (battle.phase == BP_VICTORY && battle.dialogue == DLG_NONE && !battleBusy()) {
            if (battle.hacked)
                DrawText(">> SYSTEM REPROGRAMMED! [Z/CLICK] <<", 35, PANEL_Y + 90, 20, (Color) { 120, 255, 220, 255 });
            else if (battle.trainer >= 0)
                DrawText(">> VICTORY! [Z/CLICK] TO CONTINUE <<", 35, PANEL_Y + 90, 20, (Color) { 255, 220, 100, 255 });
            else
                DrawText(">> TARGET SCRAPPED! [Z/CLICK] <<", 35, PANEL_Y + 90, 20, (Color) { 120, 255, 180, 255 });
            DrawText(TextFormat("+%d REVISION DATA", battle.dataEarned), 480, PANEL_Y + 90, 20, (Color) { 200, 170, 255, 255 });
        }
        if ((battle.phase == BP_VICTORY || battle.phase == BP_DEFEAT) && battle.dialogue == DLG_NONE && battle.loot[0])
            DrawText(battle.loot, 35, PANEL_Y + 62, 14, (Color) { 255, 220, 120, 255 });
        if (battle.phase == BP_DEFEAT && battle.dialogue == DLG_NONE)
            DrawText(">> MECH DISABLED! [Z/CLICK] TO CONTINUE <<", 35, PANEL_Y + 90, 20, (Color) { 255, 100, 100, 255 });

        if (battle.dialogue != DLG_NONE) {
            DrawRectangle(20, PANEL_Y - 110, SCREEN_W - 40, 100, (Color) { 10, 15, 30, 245 });
            DrawRectangleLines(20, PANEL_Y - 110, SCREEN_W - 40, 100, (Color) { 255, 220, 100, 240 });
            if (battle.trainer >= 0) {
                Trainer* t = &trainers[battle.trainer];
                DrawText(TextFormat("%s  %s", factions[t->faction].name, t->name), 35, PANEL_Y - 102, 14, factions[t->faction].color);
            }
            DrawText(battle.dialogueText, 40, PANEL_Y - 75, 22, (Color) { 255, 240, 220, 255 });
            DrawText("[Z/CLICK] to continue", SCREEN_W - 220, PANEL_Y - 30, 16, (Color) { 150, 200, 255, 255 });
        }
    }
    if (logOpen) drawLogOverlay();
    else if (infoOpen && p) {   // keyboard route to the same breakdown the weapon tooltip shows
        Explanation why;
        if (battleExplainPlayer(weaponSel, &why)) {
            const Weapon* w = mechWeapon(battleFieldPlayer()->mech, weaponSel);
            tip.active = 1;
            tip.fixed = 1;
            tip.hasWhy = 1;
            tip.why = why;
            snprintf(tip.title, sizeof(tip.title), "%s  [I] close", w->name);
            snprintf(tip.body, sizeof(tip.body), "%s", munitionPlain(w->munition));
        }
    }
    tipDraw();
    EndMode2D();
}

// ============ FIRMWARE REVISION SCREEN ============
static int revFrom = 0, revTo = 0, optSel = 0, branchSel = 0;
static float revTimer = 0;

static Rectangle optionRect(int i) {
    int col = i % 3, row = i / 3;
    return (Rectangle) { 130.0f + col * 185, 430.0f + row * 50, 175, 40 };
}

void uiRevisionOpen(void) {
    revFrom = battle.oldRevision;
    revTo = rosterActive()->fw.revision;
    optSel = 0;
    branchSel = 0;
    revTimer = 0;
}

void uiRevisionUpdate(float dt, GameState* state) {
    Mech* m = rosterActive();
    revTimer += dt;
    updateEffects(dt);
    if (GetRandomValue(0, 100) < 50) {
        Color accent = mechModel(m)->accent;
        Vector2 pos = { (float)(160 + GetRandomValue(-90, 90)), (float)(260 + GetRandomValue(-90, 90)) };
        spawnParticle(pos, (Vector2) { 0, -30 - (float)GetRandomValue(0, 80) }, 0.9f, 3, accent);
    }

    if (m->fw.optPoints > 0) {
        if (RIGHT_PRESSED) optSel = (optSel + 1) % NUM_OPTIMIZATIONS;
        if (LEFT_PRESSED)  optSel = (optSel + NUM_OPTIMIZATIONS - 1) % NUM_OPTIMIZATIONS;
        if (DOWN_PRESSED)  optSel = (optSel + 3) % NUM_OPTIMIZATIONS;
        if (UP_PRESSED)    optSel = (optSel + NUM_OPTIMIZATIONS - 3) % NUM_OPTIMIZATIONS;
        int activate = confirmPressed();
        for (int i = 0; i < NUM_OPTIMIZATIONS; i++) {
            if (mouseMoved() && mouseOver(optionRect(i))) optSel = i;
            if (clickedOn(optionRect(i))) { optSel = i; activate = 1; }
        }
        if (activate) {
            consumeInput();
            firmwareSpendOptimization(&m->fw, optSel);
            mechRefreshStats(m);
        }
        return;
    }
    if (firmwareBranchesPending(&m->fw) > 0) {   // major revision: choose a Firmware Branch
        if (RIGHT_PRESSED) branchSel = (branchSel + 1) % NUM_BRANCHES;
        if (LEFT_PRESSED)  branchSel = (branchSel + NUM_BRANCHES - 1) % NUM_BRANCHES;
        if (DOWN_PRESSED)  branchSel = (branchSel + 3) % NUM_BRANCHES;
        if (UP_PRESSED)    branchSel = (branchSel + NUM_BRANCHES - 3) % NUM_BRANCHES;
        int activate = confirmPressed();
        for (int i = 0; i < NUM_BRANCHES; i++) {
            if (mouseMoved() && mouseOver(optionRect(i))) branchSel = i;
            if (clickedOn(optionRect(i))) { branchSel = i; activate = 1; }
        }
        if (activate && !firmwareHasBranch(&m->fw, branchSel)) {
            consumeInput();
            firmwarePickBranch(&m->fw, branchSel);
            mechRefreshStats(m);
        }
        return;
    }
    if (revTimer > 0.4f && (confirmPressed() || clickPressed())) {
        consumeInput();
        *state = STATE_OVERWORLD;
    }
}

void uiRevisionDraw(void) {
    Mech* m = rosterActive();
    const MechModel* model = mechModel(m);
    ClearBackground((Color) { 5, 8, 18, 255 });
    drawStarfield(120 * screenW / SCREEN_W, 40);
    BeginMode2D(layoutCamera());

    float p = 0.5f + 0.5f * sinf(glowTimer * 3);
    DrawCircle(160, 260, 110 + p * 20, (Color) { model->body.r, model->body.g, model->body.b, 60 });
    DrawCircle(160, 260, 90 + p * 14, (Color) { model->accent.r, model->accent.g, model->accent.b, 80 });
    DrawCircleLines(160, 260, 130, model->accent);
    drawMechBattle(m->model, 160, 260, 14, 0);
    drawParticles();

    drawTextCentered("FIRMWARE REVISION", 30, 44, (Color) { 200, 240, 255, 255 });
    DrawText(m->name, 320, 100, 26, model->accent);
    DrawText(TextFormat("REVISION %s  >>  %s", firmwareLabel(revFrom), firmwareLabel(revTo)), 320, 132, 22, WHITE);

    int y = 172;
    for (int r = revFrom + 1; r <= revTo && y < 330; r++, y += 22)
        DrawText(TextFormat("%s   %s", firmwareLabel(r), firmwareUnlockDesc(firmwareUnlockAt(r))), 320, y, 14,
            firmwareUnlockAt(r) == UNLOCK_MAJOR ? (Color) { 255, 220, 120, 255 } : (Color) { 170, 210, 240, 255 });

    Firmware before;
    firmwareInit(&before, revFrom);
    DrawText(TextFormat("Instruction Sockets   %d  >>  %d", firmwareSockets(&before), firmwareSockets(&m->fw)),
        320, 340, 16, (Color) { 120, 240, 255, 255 });
    DrawText(TextFormat("Processing Capacity   %d  >>  %d", firmwareCapacity(&before), firmwareCapacity(&m->fw)),
        320, 362, 16, (Color) { 120, 240, 255, 255 });

    if (m->fw.optPoints > 0) {
        drawTextCentered(TextFormat("OPTIMIZATION POINTS: %d  -  choose an upgrade", m->fw.optPoints), 400, 18,
            (Color) { 255, 220, 120, 255 });
        for (int i = 0; i < NUM_OPTIMIZATIONS; i++) {
            const Optimization* o = &optimizations[i];
            drawButton(optionRect(i), TextFormat("+%d %s", o->amount, statName(o->stat)), 16, i == optSel, 1);
        }
        drawTextCentered("[WASD/ARROWS] Select   [Z/ENTER/CLICK] Install", SCREEN_H - 30, 16, (Color) { 100, 240, 255, 255 });
    }
    else if (firmwareBranchesPending(&m->fw) > 0) {
        drawTextCentered("MAJOR REVISION  -  choose a Firmware Branch", 400, 18, (Color) { 255, 220, 120, 255 });
        for (int i = 0; i < NUM_BRANCHES; i++)
            drawButton(optionRect(i), branchDefs[i].name, 14, i == branchSel, !firmwareHasBranch(&m->fw, i));
        drawTextCentered(branchDefs[branchSel].desc, 530, 14, (Color) { 200, 220, 240, 255 });
        drawTextCentered("[WASD/ARROWS] Select   [Z/ENTER/CLICK] Compile branch", SCREEN_H - 30, 16, (Color) { 100, 240, 255, 255 });
    }
    else if (revTimer > 0.4f)
        drawTextCentered("[Z/CLICK] to continue", SCREEN_H - 40, 20, (Color) { 100, 240, 255, 255 });
    EndMode2D();
}
