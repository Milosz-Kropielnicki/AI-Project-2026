#include "ui.h"
#include "game.h"
#include "battle.h"
#include "world.h"
#include "audio.h"
#include "transition.h"
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
    int munition, armorDamage, integrityDamage, lethal, crit;
} Effect;
#define MAX_EFFECTS 12
static Effect effects[MAX_EFFECTS];

// Floating combat text: Integrity damage (red), Armor damage (blue), MISS, SCRAPPED
typedef struct { int active; Vector2 pos; float life; char text[24]; int size; Color color; } DamageNum;
#define MAX_DAMAGE_NUMS 16
static DamageNum damageNums[MAX_DAMAGE_NUMS];

static float shakeAmount = 0, shakeTimer = 0;

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
    case FX_SWITCH: return 0.8f;
    case FX_PROVOKE: return 0.9f;
    case FX_GUARD: return 0.5f;
    case FX_LINK: return 0.7f;
    default: return 0.55f;
    }
}

static void addEffect(const BattleEvent* ev, Vector2 from, Vector2 to) {
    for (int i = 0; i < MAX_EFFECTS; i++)
        if (!effects[i].active) {
            effects[i] = (Effect){ 1, ev->fx, 0, effectDuration(ev->fx), from, to, ev->color, 0, ev->damage, ev->hit,
                ev->munition, ev->armorDamage, ev->integrityDamage, ev->lethal, ev->crit };
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
        if (e->crit) {
            addFloatText((Vector2) { e->to.x - 60, e->to.y - 66 }, "CRITICAL", 22, (Color) { 255, 240, 120, 255 });
            sfxPlay(SFX_CRIT);
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
        case FX_SWITCH:   // the new mech drops in on a column of light
            if (p < 0.6f && GetRandomValue(0, 100) < 70)
                spawnParticleG((Vector2) { e->to.x + frandRange(-28, 28), e->to.y + 40 }, (Vector2) { 0, -frandRange(120, 260) },
                    0.5f, 3, e->color, 0);
            if (!e->damageShown && p >= 0.55f) { e->damageShown = 1; spawnBurst(e->to, 36, e->color, 60, 220, 0.7f); shakeScreen(4, 0.2f); }
            break;
        case FX_GUARD:   // the Aegis pulls its share of the hit across the shield link
            if (p < 0.6f && GetRandomValue(0, 100) < 80) spawnParticleG(cur, (Vector2) { 0, -20 }, 0.3f, 3, e->color, 0);
            if (!e->damageShown && p >= 0.6f) { e->damageShown = 1; impact(e, 10, 40, 120, 0.4f, 2, 0.15f, 0); }
            break;
        case FX_LINK:    // uplink: motes run from the initiator to the partner
            if (p < 0.9f) spawnParticleG(cur, (Vector2) { 0, 0 }, 0.35f, 3, e->color, 0);
            if (!e->damageShown && p >= 0.9f) { e->damageShown = 1; spawnBurst(e->to, 18, e->color, 30, 110, 0.5f); }
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
        case FX_PROVOKE: {   // expanding war-horn rings and a banner
            for (int k = 0; k < 3; k++) {
                float q = p - k * 0.15f;
                if (q <= 0) continue;
                DrawCircleLines((int)e->to.x, (int)e->to.y, 20 + q * 70, (Color) { 255, 150, 60, (unsigned char)(220 * (1 - q)) });
            }
            const char* txt = "PROVOKE!";
            DrawText(txt, (int)(e->to.x - MeasureText(txt, 14) / 2), (int)(e->to.y - 62), 14, (Color) { 255, 170, 80, (unsigned char)(255 * (1 - p * 0.5f)) });
            break;
        }
        case FX_GUARD: {
            float a = p < 0.7f ? 1 : (1 - p) / 0.3f;
            DrawLineEx(e->from, e->to, 6, (Color) { e->color.r, e->color.g, e->color.b, (unsigned char)(70 * a) });
            DrawLineEx(e->from, e->to, 2, (Color) { 255, 255, 255, (unsigned char)(180 * a) });
            DrawCircleLines((int)e->from.x, (int)e->from.y, 34 + 6 * p, (Color) { e->color.r, e->color.g, e->color.b, (unsigned char)(200 * a) });
            const char* txt = "SHIELDED";
            DrawText(txt, (int)(e->from.x - MeasureText(txt, 10) / 2), (int)(e->from.y - 58), 10, (Color) { e->color.r, e->color.g, e->color.b, (unsigned char)(255 * a) });
            break;
        }
        case FX_LINK: {
            Vector2 tip = { e->from.x + (e->to.x - e->from.x) * (p < 0.6f ? p / 0.6f : 1), e->from.y + (e->to.y - e->from.y) * (p < 0.6f ? p / 0.6f : 1) };
            unsigned char a = (unsigned char)(255 * (p < 0.7f ? 1 : (1 - p) / 0.3f));
            DrawLineEx(e->from, tip, 4, (Color) { e->color.r, e->color.g, e->color.b, a });
            DrawCircleV(tip, 5, (Color) { 255, 255, 255, a });
            const char* txt = "LINKED";
            DrawText(txt, (int)(e->to.x - MeasureText(txt, 12) / 2), (int)(e->to.y - 62), 12, (Color) { e->color.r, e->color.g, e->color.b, a });
            break;
        }
        case FX_SWITCH: {
            float h = 110 * (p < 0.5f ? p * 2 : 1), a = p < 0.5f ? 1 : (1 - p) * 2;
            DrawRectangle((int)e->to.x - 34, (int)(e->to.y + 48 - h), 68, (int)h, (Color) { e->color.r, e->color.g, e->color.b, (unsigned char)(70 * a) });
            DrawRectangle((int)e->to.x - 11, (int)(e->to.y + 48 - h), 22, (int)h, (Color) { 255, 255, 255, (unsigned char)(90 * a) });
            const char* txt = "SWITCH IN";
            DrawText(txt, (int)(e->to.x - MeasureText(txt, 12) / 2), (int)(e->to.y - 66), 12, (Color) { 255, 255, 255, (unsigned char)(255 * a) });
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
static int pickerOpen = 0, pickerSel = 0, pickerDeploy = 0;   // [S] switch / [D] deploy picker
static int autoDeployRound = -1;   // the deploy picker opens by itself once per round
static int lastPhase = -1, heatWasCritical = 0, lastActor = -1;
static const LogEntry* lastSeenLog = NULL;

// Layout (3v3): enemy HUDs down the left, the player's down the right with the
// commanded mech's reactor and the team cards under them, the machines in the
// middle (enemy row above, player row below), the log strip, then the bottom
// panel: weapons of the commanded mech on the left, the preview on the right.
#define PANEL_Y (SCREEN_H - 180)
#define ROW_Y (PANEL_Y + 26)
#define SPRITE_SCALE 7

// Formation rows: each side's Front row faces the other side, the Rear row sits
// behind it, and a flanking mech steps out past its Front row
#define ENEMY_REAR_Y 70
#define ENEMY_FRONT_Y 112
#define ENEMY_FLANK_Y 134
#define PLAYER_FRONT_Y 288
#define PLAYER_REAR_Y 326
#define PLAYER_FLANK_Y 266
static float rowY[2][MAX_FIELD];    // eased, so a mech slides when it changes position

static float laneRowY(int side, int pos) {
    const Combatant* c = battleField(side, pos);
    if (side == SIDE_PLAYER)
        return !c ? (PLAYER_FRONT_Y + PLAYER_REAR_Y) / 2.0f : c->flanking ? PLAYER_FLANK_Y : c->lane == LANE_FRONT ? PLAYER_FRONT_Y : PLAYER_REAR_Y;
    return !c ? (ENEMY_FRONT_Y + ENEMY_REAR_Y) / 2.0f : c->flanking ? ENEMY_FLANK_Y : c->lane == LANE_FRONT ? ENEMY_FRONT_Y : ENEMY_REAR_Y;
}

static void easeRows(float dt) {
    float k = dt > 0 ? (dt * 10 < 1 ? dt * 10 : 1) : 1;
    for (int side = 0; side < 2; side++)
        for (int p = 0; p < MAX_FIELD; p++) {
            float to = laneRowY(side, p);
            rowY[side][p] = rowY[side][p] == 0 ? to : rowY[side][p] + (to - rowY[side][p]) * k;
        }
}

static Vector2 slotPos(int side, int pos) {
    static const float x[MAX_FIELD] = { 318, 400, 482 };
    if (pos < 0 || pos >= MAX_FIELD) pos = 0;
    return (Vector2) { x[pos], rowY[side][pos] != 0 ? rowY[side][pos] : laneRowY(side, pos) };
}
static Rectangle spriteRect(int side, int pos) {
    Vector2 v = slotPos(side, pos);
    return (Rectangle) { v.x - 38, v.y - 44, 76, 88 };
}
static Rectangle hudRect(int side, int pos) {
    return side == SIDE_PLAYER ? (Rectangle) { 552, 8.0f + pos * 78, 240, 74 } : (Rectangle) { 8, 8.0f + pos * 72, 240, 68 };
}
static Rectangle weaponButtonRect(int i) { return (Rectangle) { 30, (float)(ROW_Y + i * 34), 355, 30 }; }
static Rectangle deployButtonRect(void) { return (Rectangle) { 240, PANEL_Y + 4, 82, 18 }; }
static Rectangle switchButtonRect(void) { return (Rectangle) { 326, PANEL_Y + 4, 82, 18 }; }
static Rectangle provokeButtonRect(void) { return (Rectangle) { 412, PANEL_Y + 4, 92, 18 }; }
static Rectangle hackButtonRect(void) { return (Rectangle) { 508, PANEL_Y + 4, 72, 18 }; }
static Rectangle nextButtonRect(void) { return (Rectangle) { 584, PANEL_Y + 4, 86, 18 }; }
static Rectangle endTurnButtonRect(void) { return (Rectangle) { 674, PANEL_Y + 4, 96, 18 }; }
static Rectangle laneButtonRect(void) { return (Rectangle) { 552, 381, 118, 15 }; }
static Rectangle linkButtonRect(void) { return (Rectangle) { 176, 293, 72, 14 }; }
#define LINK_PANEL_Y 294
static Rectangle flankButtonRect(void) { return (Rectangle) { 674, 381, 118, 15 }; }
static Rectangle logStripRect(void) { return (Rectangle) { 20, PANEL_Y - 22, SCREEN_W - 40, 20 }; }
static Rectangle reserveCardRect(int i) { return (Rectangle) { 552.0f + i * 40, 302, 37, 40 }; }
#define PICKER_X 130
#define PICKER_W 540
#define PICKER_ROW 46
static int pickerRows(int* slots) {   // standing reserves; a forced deploy adds a last row to yield
    int n = battleSwitchList(slots, MAX_TEAM);
    if (pickerDeploy && battle.phase == BP_DEPLOY) slots[n++] = -1;
    return n;
}
static int pickerTop(int rows) { return 250 - (rows * PICKER_ROW + 80) / 2; }
static Rectangle pickerRowRect(int i, int rows) {
    return (Rectangle) { PICKER_X + 10, (float)(pickerTop(rows) + 40 + i * PICKER_ROW), PICKER_W - 20, PICKER_ROW - 4 };
}
#define HEAT_CRITICAL 0.75f

void uiBattleOpen(void) {
    weaponSel = 0;
    reselectAfterShot = 0;
    logOpen = infoOpen = pickerOpen = 0;
    autoDeployRound = -1;
    lastPhase = -1;
    lastActor = -1;
    heatWasCritical = 0;
    lastSeenLog = NULL;
    clearEffects();
    memset(rowY, 0, sizeof(rowY));
    easeRows(0);   // snap into formation
    // IMPORTANT: do NOT reset the transition here. It was handed off from the
    // world side (phase 3 = opening) and needs to survive into the battle so
    // the panels can reverse and reveal the fight.
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
    if (critical && !heatWasCritical && lastActor == battle.actingSlot) sfxPlay(SFX_HEAT_WARN);   // not when just tabbing to a hot mech
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

static void openPicker(int deploy, int preselectSlot) {
    pickerOpen = 1;
    pickerDeploy = deploy;
    pickerSel = 0;
    int slots[MAX_TEAM + 1], n = pickerRows(slots);
    for (int i = 0; i < n; i++) if (slots[i] == preselectSlot) pickerSel = i;
}

// Switch picker (the commanded mech swaps with a reserve) / deploy picker (a
// reserve fills an empty position; forced when nobody is on the field)
static void updatePicker(void) {
    int forced = pickerDeploy && battle.phase == BP_DEPLOY;
    int slots[MAX_TEAM + 1], n = pickerRows(slots);
    int valid = pickerDeploy ? battleCanDeploy() : battleCanSwitch(NULL);
    if ((!forced && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_X))) || !valid || n == 0) {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_X)) consumeInput();
        pickerOpen = 0;
        return;
    }
    if (pickerSel >= n) pickerSel = n - 1;
    if (DOWN_PRESSED) { pickerSel = (pickerSel + 1) % n; sfxPlay(SFX_UI_MOVE); }
    if (UP_PRESSED) { pickerSel = (pickerSel + n - 1) % n; sfxPlay(SFX_UI_MOVE); }
    int activate = confirmPressed();
    for (int i = 0; i < n; i++) {
        if (mouseMoved() && mouseOver(pickerRowRect(i, n))) pickerSel = i;
        if (clickedOn(pickerRowRect(i, n))) { pickerSel = i; activate = 1; }
    }
    if (!activate) return;
    consumeInput();
    pickerOpen = 0;
    if (slots[pickerSel] < 0) battleYield();
    else if (pickerDeploy) battleDeploy(slots[pickerSel]);
    else battleSwitchTo(slots[pickerSel]);
    reselectAfterShot = 1;
}

void uiBattleUpdate(float dt, GameState* state) {
    // While the wipe is opening over the battle, tick the transition and
    // ease rows / effects so nothing snaps when the panels lift - but do
    // NOT run the battle logic. The fight is frozen behind the closed
    // panels so the reveal shows its opening state, not an already-started
    // battle. Once phase 3 ends, the next frame runs normally.
    if (transition.active && transition.phase == 3) {
        transitionUpdate(dt);
        easeRows(dt);
        updateEffects(dt);
        return;
    }

    battleUpdate(dt);
    easeRows(dt);
    updateEffects(dt);
    BattleEvent ev;
    while (battlePopEvent(&ev)) {
        int own = ev.fromPlayer ? SIDE_PLAYER : SIDE_ENEMY, other = ev.fromPlayer ? SIDE_ENEMY : SIDE_PLAYER;
        if (ev.fx == FX_SWITCH || ev.fx == FX_PROVOKE) {
            Vector2 at = slotPos(own, ev.toSlot);
            addEffect(&ev, at, at);
            sfxPlay(ev.fx == FX_SWITCH ? SFX_SWITCH : SFX_PROVOKE);
            continue;
        }
        if (ev.fx == FX_GUARD || ev.fx == FX_LINK) {   // between two mechs on the same side
            addEffect(&ev, slotPos(own, ev.fromSlot), slotPos(own, ev.toSlot));
            if (ev.fx == FX_LINK) sfxPlay(SFX_LINK);
            continue;
        }
        addEffect(&ev, slotPos(own, ev.fromSlot), slotPos(other, ev.toSlot));
        if (ev.munition >= 0) sfxFire(ev.munition);
        else if (ev.fx == FX_SCAN) sfxPlay(SFX_HACK);
    }
    battleSounds();
    if (battle.actingSlot != lastActor) { lastActor = battle.actingSlot; reselectAfterShot = 1; }

    if (logOpen) { updateLogOverlay(); return; }
    if (IsKeyPressed(KEY_L) || clickedOn(logStripRect())) { consumeInput(); logOpen = 1; logSel = 0; return; }
    if (IsKeyPressed(KEY_I)) infoOpen = !infoOpen;
    if (IsKeyPressed(KEY_V)) formulaView = !formulaView;

    if (battle.testRange && !pickerOpen) {
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
    // Deploy picker: forced with nobody on the field, offered once a round otherwise
    if (!pickerOpen && !battleBusy() && battleCanDeploy()
        && (battle.phase == BP_DEPLOY || autoDeployRound != battle.round)) {
        autoDeployRound = battle.round;
        openPicker(1, -1);
    }
    if (pickerOpen) { updatePicker(); return; }
    if (battle.phase != BP_PLAYER_TURN || battleBusy()) return;

    // Which mech, which target: TAB / click a friendly, Q / E / click an enemy
    if (IsKeyPressed(KEY_TAB) || clickedOn(nextButtonRect())) { consumeInput(); battleNextActor(); sfxPlay(SFX_UI_MOVE); return; }
    if (IsKeyPressed(KEY_Q)) { battleCycleTarget(-1); sfxPlay(SFX_UI_MOVE); }
    if (IsKeyPressed(KEY_E)) { battleCycleTarget(1); sfxPlay(SFX_UI_MOVE); }
    for (int p = 0; p < MAX_FIELD; p++) {
        if (battleField(SIDE_ENEMY, p) && (clickedOn(hudRect(SIDE_ENEMY, p)) || clickedOn(spriteRect(SIDE_ENEMY, p)))) {
            consumeInput(); battleSetTarget(p); sfxPlay(SFX_UI_MOVE); return;
        }
        if (battleField(SIDE_PLAYER, p) && (clickedOn(hudRect(SIDE_PLAYER, p)) || clickedOn(spriteRect(SIDE_PLAYER, p)))) {
            consumeInput(); battleSelectActor(p); sfxPlay(SFX_UI_MOVE); return;
        }
    }
    if (reselectAfterShot) { reselectAfterShot = 0; selectFireableWeapon(); }

    // S is the switch key here, so weapons cycle with the arrow keys
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_RIGHT)) { weaponSel = (weaponSel + 1) % MAX_WEAPONS; sfxPlay(SFX_UI_MOVE); }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_LEFT))    { weaponSel = (weaponSel + MAX_WEAPONS - 1) % MAX_WEAPONS; sfxPlay(SFX_UI_MOVE); }

    // Mouse: hovering selects a weapon (and shows its preview), clicking fires it
    int clickedWeapon = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) {
        if (mouseMoved() && mouseOver(weaponButtonRect(i))) weaponSel = i;
        if (clickedOn(weaponButtonRect(i))) { weaponSel = i; clickedWeapon = 1; }
    }

    // [D] deploy, [S] switch (or click a reserve card)
    int cardSlot = -1;
    for (int i = 0; i < battle.side[SIDE_PLAYER].count; i++)
        if (clickedOn(reserveCardRect(i)) && battleSlotStanding(SIDE_PLAYER, i) && !battleSlotOnField(SIDE_PLAYER, i)) cardSlot = i;
    if (IsKeyPressed(KEY_D) || clickedOn(deployButtonRect())) {
        consumeInput();
        if (battleCanDeploy()) { openPicker(1, -1); sfxPlay(SFX_UI_CONFIRM); }
        else { snprintf(battle.log, sizeof(battle.log), "DEPLOY: no empty position or no reserve left."); sfxPlay(SFX_UI_DENY); }
        return;
    }
    if (IsKeyPressed(KEY_S) || clickedOn(switchButtonRect()) || cardSlot >= 0) {
        consumeInput();
        const char* reason = NULL;
        if (cardSlot >= 0 && battleCanDeploy()) { openPicker(1, cardSlot); sfxPlay(SFX_UI_CONFIRM); }
        else if (battleCanSwitch(&reason)) { openPicker(0, cardSlot); sfxPlay(SFX_UI_CONFIRM); }
        else {
            snprintf(battle.log, sizeof(battle.log), "SWITCH: %s", reason);
            sfxPlay(SFX_UI_DENY);
        }
        return;
    }
    if (IsKeyPressed(KEY_P) || clickedOn(provokeButtonRect())) {
        consumeInput();
        const char* reason = NULL;
        if (battleCanProvoke(&reason)) battleProvoke();
        else { snprintf(battle.log, sizeof(battle.log), "PROVOKE: %s", reason); sfxPlay(SFX_UI_DENY); }
        return;
    }
    // [K] link (or re-link) the commanded initiator
    if (IsKeyPressed(KEY_K) || clickedOn(linkButtonRect())) {
        consumeInput();
        const char* reason = NULL;
        if (battleCanLink(&reason)) battleLink();
        else { snprintf(battle.log, sizeof(battle.log), "LINK: %s", reason); sfxPlay(SFX_UI_DENY); }
        return;
    }
    // [F] swap lanes, [G] go out on the flank
    int laneKey = IsKeyPressed(KEY_F) || clickedOn(laneButtonRect());
    int flankKey = IsKeyPressed(KEY_G) || clickedOn(flankButtonRect());
    if (laneKey || flankKey) {
        consumeInput();
        const Combatant* a = battleActing();
        int to = flankKey ? MOVE_FLANK : a && a->lane == LANE_FRONT ? LANE_REAR : LANE_FRONT;
        const char* reason = NULL;
        if (battleCanMove(to, &reason)) { battleMove(to); sfxPlay(SFX_UI_CONFIRM); }
        else { snprintf(battle.log, sizeof(battle.log), "%s: %s", flankKey ? "FLANK" : "MOVE", reason); sfxPlay(SFX_UI_DENY); }
        return;
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

// Compact effective values; red/green when battle modifiers move them off the base stat
static void drawReadouts(const MechStats* s, int mobility, int accuracy, int x, int y) {
    Color base = { 170, 200, 230, 255 }, down = { 255, 130, 120, 255 }, up = { 120, 255, 160, 255 };
    DrawText(TextFormat("ACC %d", accuracy), x, y, 10, accuracy < s->accuracy ? down : accuracy > s->accuracy ? up : base);
    DrawText(TextFormat("MOB %d", mobility), x + 56, y, 10, mobility > s->mobility ? up : base);
    DrawText(TextFormat("STB %d", s->stability), x + 112, y, 10, base);
    DrawText(TextFormat("PWR %.2f", s->power), x + 168, y, 10, base);
    tipText((Rectangle) { (float)x, (float)y, 52, 11 }, TextFormat("ACCURACY %d%%", accuracy),
        TextFormat("Multiplies the hit chance of every weapon this machine fires. Base %d%%; firmware (Precision Strike, "
            "Recursive Targeting) raises it for this turn, scrambles lower it.", s->accuracy));
    tipText((Rectangle) { (float)x + 56, (float)y, 52, 11 }, TextFormat("MOBILITY %d%%", mobility),
        TextFormat("Chance to dodge: every attack against it hits %d%% less often (Mobility / 2). Evasive firmware "
            "adds to it until its next turn.", mobility / 2));
    tipText((Rectangle) { (float)x + 112, (float)y, 52, 11 }, TextFormat("STABILITY %d%%", s->stability),
        "Resistance to scrambles, corruption and hacking. A scramble of strength S lands with chance S / (S + Stability).");
    tipText((Rectangle) { (float)x + 168, (float)y, 60, 11 }, TextFormat("POWER %.2fx", s->power),
        "Multiplies the base damage of every weapon this machine fires.");
}

static int previewSelected(AttackPreview* p) {
    return battle.phase == BP_PLAYER_TURN && battle.dialogue == DLG_NONE && battlePreviewPlayer(weaponSel, p);
}

// What the selected weapon would do to each enemy position (primary + splash), for the HUD ghosts.
// framePrimary is where the shot lands first: the target, or the Front guard covering it.
static int framePreviewOk[MAX_FIELD];
static AttackPreview framePreview[MAX_FIELD];
static int framePrimary = -1;
static void computeFramePreviews(void) {
    memset(framePreviewOk, 0, sizeof(framePreviewOk));
    framePrimary = -1;
    if (battle.phase != BP_PLAYER_TURN || battle.dialogue != DLG_NONE || battleBusy()) return;
    int pos[MAX_FIELD];
    AttackPreview all[MAX_FIELD];
    int n = battlePreviewTargets(weaponSel, pos, all, MAX_FIELD);
    for (int k = 0; k < n; k++) { framePreview[pos[k]] = all[k]; framePreviewOk[pos[k]] = 1; }
    if (n > 0) framePrimary = pos[0];
}
static int intercepted(void) { return framePrimary >= 0 && framePrimary != battle.playerTarget; }
// The enemy the selected weapon actually hits first
static const Combatant* previewVictim(void) {
    const Combatant* c = battleField(SIDE_ENEMY, framePrimary);
    return c ? c : battleFieldEnemy();
}

static const char* threatRules(void) {
    return TextFormat("How much the enemy AI wants to shoot this mech: each attack's value is multiplied by 1 + threat/100. "
        "Attacking +%d a turn (+%d with area, cone or heavy weapons), repair +%d, buff +%d, PROVOKE +%d, +%d per round on the "
        "field (an Ironclad +%d); -%d at the start of every round. Max %d.", THREAT_ATTACK, THREAT_HEAVY_ATTACK, THREAT_REPAIR,
        THREAT_BUFF, THREAT_PROVOKE, THREAT_PASSIVE, THREAT_PASSIVE_IRONCLAD, THREAT_DECAY, THREAT_MAX);
}

// Threat bar: yellow to red as it fills, pulsing orange frame while provoking
static void drawThreatBar(int x, int y, int w, int h, const Combatant* c) {
    float f = c->threat / (float)THREAT_MAX;
    Color fill = { 255, (unsigned char)(220 - 170 * f), 60, 255 };
    DrawRectangle(x, y, w, h, (Color) { 30, 24, 20, 255 });
    DrawRectangle(x, y, (int)(w * f), h, fill);
    if (c->provoking) {
        float blink = 0.5f + 0.5f * sinf(glowTimer * 8);
        DrawRectangleLines(x - 1, y - 1, w + 2, h + 2, (Color) { 255, 150, 60, (unsigned char)(150 + 105 * blink) });
    }
}

// FRONT / REAR / FLANK tag, right-aligned at rx. A Rear mech nobody covers is red.
static Color laneColor(int side, int pos, const Combatant* c) {
    if (c->flanking) return (Color) { 255, 160, 70, 255 };
    if (c->lane == LANE_FRONT) return (Color) { 210, 220, 240, 255 };
    return battleInterceptor(side, pos) >= 0 ? (Color) { 120, 230, 160, 255 } : (Color) { 255, 110, 100, 255 };
}

static void drawLaneBadge(int rx, int y, int side, int pos, const Combatant* c) {
    const char* t = c->flanking ? "FLANK" : c->lane == LANE_FRONT ? "FRONT" : "REAR";
    Color col = laneColor(side, pos, c);
    int w = MeasureText(t, 10) + 8;
    Rectangle r = { (float)(rx - w), (float)y, (float)w, 12 };
    DrawRectangleRec(r, (Color) { col.r / 6, col.g / 6, col.b / 6, 230 });
    DrawRectangleLinesEx(r, 1, col);
    DrawText(t, rx - w + 4, y + 1, 10, col);
    int guard = battleInterceptor(side, pos);
    const char* move = side == SIDE_PLAYER ? TextFormat(" [F] swaps lanes: %s.", battleMoveCost(c) ? "1 EN" : "free for Recon / EW") : "";
    const char* body;
    if (c->flanking)
        body = TextFormat("Out on the flank until its next turn: its attacks ignore %d%% of the target's Armor, it is at -%d Mobility, "
            "and it is out of formation - it doesn't cover the Rear and nothing covers it.", (int)roundf(FLANK_ARMOR_IGNORE * 100),
            FLANK_MOBILITY_PENALTY);
    else if (c->lane == LANE_FRONT)
        body = TextFormat("Front row: single-target and line attacks aimed at a Rear ally hit this mech instead. Area and cone "
            "weapons ignore formation.%s", move);
    else if (guard >= 0)
        body = TextFormat("Rear row, COVERED by %s: single-target and line attacks aimed here hit it instead. Area and cone weapons "
            "still reach this mech.%s", battleField(side, guard)->mech->name, move);
    else
        body = TextFormat("Rear row, but EXPOSED: %s, so attacks land here directly.%s",
            c->provoking ? "a provoking mech gives up its cover" : "no Front ally is in formation", move);
    tipText(r, c->flanking ? "FLANK" : c->lane == LANE_FRONT ? "FRONT" : guard >= 0 ? "REAR (COVERED)" : "REAR (EXPOSED)", body);
}

// "  PERK  NAME: what it does." for a tooltip, empty if the chassis has none
static const char* perkLine(const Mech* m) {
    const MechModel* mm = mechModel(m);
    return mm->perkName && mm->perkName[0] ? TextFormat("  PERK  %s: %s", mm->perkName, mm->perkDesc) : "";
}

static void drawEmptySlot(Rectangle r, const char* label, const char* sub) {
    DrawRectangleRec(r, (Color) { 12, 16, 28, 200 });
    for (float x = r.x; x < r.x + r.width; x += 10) {
        DrawLine((int)x, (int)r.y, (int)(x + 5), (int)r.y, (Color) { 70, 90, 120, 200 });
        DrawLine((int)x, (int)(r.y + r.height), (int)(x + 5), (int)(r.y + r.height), (Color) { 70, 90, 120, 200 });
    }
    DrawText(label, (int)r.x + 10, (int)r.y + 14, 14, (Color) { 90, 110, 140, 255 });
    if (sub) DrawText(sub, (int)r.x + 10, (int)r.y + 34, 10, (Color) { 120, 200, 240, 255 });
}

static const char* pendingText(const Combatant* c) {
    int pending = c->nextSkipTurn + (c->nextDisabledWeapon >= 0) + (c->nextAccPenalty > 0) + c->nextEnergyLoss
        + c->nextEnergyTax + c->nextRandomTargeting;
    return pending ? TextFormat("%d QUEUED", pending) : NULL;
}

// One enemy on the field; the target gets a pulsing frame and the selected
// weapon's damage ghosts (splash targets too)
static void drawEnemySlot(int pos) {
    Rectangle r = hudRect(SIDE_ENEMY, pos);
    const Combatant* c = battleField(SIDE_ENEMY, pos);
    if (!c) { drawEmptySlot(r, TextFormat("POSITION %d  EMPTY", pos + 1), battleSideStanding(SIDE_ENEMY) > battleFieldCount(SIDE_ENEMY)
        ? "A reserve moves up next enemy phase" : NULL); return; }
    int x = (int)r.x, y = (int)r.y, target = pos == battle.playerTarget && battle.phase == BP_PLAYER_TURN;
    const Mech* m = c->mech;
    const MechStats* s = &m->stats;
    float blink = 0.5f + 0.5f * sinf(glowTimer * 6);
    DrawRectangleRec(r, (Color) { 24, 20, 34, 235 });
    Color edge = target ? (Color) { 255, 220, 80, (unsigned char)(170 + 85 * blink) } : (Color) { 200, 80, 80, 200 };
    DrawRectangleLinesEx(r, target ? 2.0f : 1.0f, edge);
    DrawText(m->name, x + 8, y + 4, 14, (Color) { 255, 210, 210, 255 });
    drawLaneBadge(x + 234, y + 4, SIDE_ENEMY, pos, c);
    int guarding = framePrimary == pos && intercepted();
    if (battle.phase == BP_PLAYER_TURN && battleHidden(pos)) DrawText("NO SIGNAL", x + 124, y + 6, 10, (Color) { 210, 160, 255, 255 });
    else if (target) DrawText(intercepted() ? "COVERED" : "TARGET", x + 124, y + 6, 10,
        intercepted() ? (Color) { 120, 220, 255, 255 } : (Color) { 255, 220, 80, 255 });
    else if (guarding) DrawText("GUARDS", x + 124, y + 6, 10, (Color) { 120, 220, 255, 255 });
    else if (c->provoking) DrawText("PROVOKE", x + 124, y + 6, 10, (Color) { 255, 150, 60, 255 });
    const char* arch = c->archetype >= 0 ? TextFormat(" [%s%s]", archetypes[c->archetype].boss ? "BOSS " : "", archetypes[c->archetype].name) : "";
    const char* fw = TextFormat("FW %s", firmwareLabel(m->fw.revision));
    const char* model = TextFormat("%s %s%s", mechModel(m)->name, roleName(mechRole(m)), arch);
    if (MeasureText(model, 10) + MeasureText(fw, 10) + 8 > 226) model = TextFormat("%s%s", roleName(mechRole(m)), arch);
    DrawText(model, x + 8, y + 19, 10, (Color) { 200, 200, 240, 255 });
    DrawText(fw, x + 234 - MeasureText(fw, 10), y + 19, 10, WHITE);
    drawIntegrityBar(x + 8, y + 32, 150, 6, s->integrity, s->maxIntegrity);
    drawArmorBar(x + 8, y + 42, 150, 5, s->armor, s->maxArmor);
    if (framePreviewOk[pos]) {
        drawLossGhost(x + 8, y + 32, 150, 6, s->integrity, framePreview[pos].integrityDamage, s->maxIntegrity);
        drawLossGhost(x + 8, y + 42, 150, 5, s->armor, framePreview[pos].armorDamage, s->maxArmor);
        if (!target && !guarding) DrawText(TextFormat("SPLASH %d%%", (int)roundf(framePreview[pos].splashMod * 100)), x + 124, y + 6, 10,
            (Color) { 255, 170, 90, 255 });
    }
    DrawText(TextFormat("INT %d/%d", s->integrity, s->maxIntegrity), x + 164, y + 30, 10, WHITE);
    DrawText(TextFormat("ARM %d/%d", s->armor, s->maxArmor), x + 164, y + 40, 10, (Color) { 150, 190, 240, 255 });
    drawReadouts(s, combatMobility(c), combatAccuracy(c), x + 8, y + 53);
    drawThreatBar(x + 8, y + 64, 224, 2, c);
    tipText((Rectangle) { (float)x + 8, (float)y + 62, 224, 6 }, TextFormat("THREAT %d%s", c->threat, c->provoking ? "  (PROVOKING)" : ""),
        c->provoking ? "It is PROVOKING: your single-target weapons must aim at it until its next turn. Area and cone weapons still reach the others."
                     : threatRules());
    tipText((Rectangle) { (float)x + 8, (float)y + 30, 230, 10 }, TextFormat("INTEGRITY %d/%d", s->integrity, s->maxIntegrity),
        "The machine's health. At 0 it is scrapped. Penetrating damage and anything Armor can't absorb lands here.");
    tipText((Rectangle) { (float)x + 8, (float)y + 41, 230, 9 }, TextFormat("ARMOR %d/%d", s->armor, s->maxArmor),
        "A second pool on top of Integrity. Each hit splits: the weapon's penetration % goes straight to Integrity, "
        "the rest is soaked by Armor until it runs out, then spills over. Armor Analysis and Siege add penetration.");
    tipText((Rectangle) { (float)x, (float)y, 118, 18 }, TextFormat("%s  FW %s", m->name, firmwareLabel(m->fw.revision)),
        TextFormat("%s%s", target && intercepted() ? TextFormat("Covered: this weapon would hit %s in the Front instead. Area and cone "
            "weapons reach it.", previewVictim()->mech->name) : "Click (or Q / E) to target this machine.", perkLine(m)));
}

// One of the player's field mechs: acting / done / locked state, pools and heat
static void drawPlayerSlot(int pos) {
    Rectangle r = hudRect(SIDE_PLAYER, pos);
    const Combatant* c = battleField(SIDE_PLAYER, pos);
    if (!c) { drawEmptySlot(r, TextFormat("POSITION %d  EMPTY", pos + 1), battleCanDeploy() ? "[D] DEPLOY A RESERVE (free)" : NULL); return; }
    int x = (int)r.x, y = (int)r.y;
    int acting = battle.phase == BP_PLAYER_TURN && pos == battle.actingSlot && battleActing() == c;
    const Mech* m = c->mech;
    const MechStats* s = &m->stats;
    Color accent = mechModel(m)->accent;
    float blink = 0.5f + 0.5f * sinf(glowTimer * 6);
    DrawRectangleRec(r, acting ? (Color) { 26, 42, 64, 240 } : (Color) { 18, 26, 42, 230 });
    Color edge = acting ? (Color) { accent.r, accent.g, accent.b, (unsigned char)(170 + 85 * blink) }
        : c->done ? (Color) { 70, 80, 100, 220 } : (Color) { accent.r, accent.g, accent.b, 160 };
    DrawRectangleLinesEx(r, acting ? 2.0f : 1.0f, edge);
    DrawText(m->name, x + 8, y + 4, 14, c->done && !acting ? (Color) { 150, 160, 180, 255 } : accent);
    drawLaneBadge(x + 234, y + 4, SIDE_PLAYER, pos, c);
    const char* tag = acting ? "ACTING" : c->skipTurn ? "SCRAMBLED" : c->done ? "DONE" : battle.phase == BP_PLAYER_TURN ? "READY [TAB]" : "";
    Color tc = acting ? accent : c->skipTurn ? (Color) { 200, 150, 255, 255 } : c->done ? (Color) { 120, 130, 150, 255 } : (Color) { 120, 255, 180, 255 };
    DrawText(tag, x + 186 - MeasureText(tag, 10), y + 6, 10, tc);
    drawIntegrityBar(x + 8, y + 22, 150, 6, s->integrity, s->maxIntegrity);
    DrawText(TextFormat("INT %d/%d", s->integrity, s->maxIntegrity), x + 164, y + 20, 10, WHITE);
    drawArmorBar(x + 8, y + 31, 150, 5, s->armor, s->maxArmor);
    DrawText(TextFormat("ARM %d/%d", s->armor, s->maxArmor), x + 164, y + 29, 10, (Color) { 150, 190, 240, 255 });
    int addHeat = acting && framePrimary >= 0 ? framePreview[framePrimary].heat : 0;
    drawHeatGauge(x + 8, y + 40, 150, 5, s->heat, s->maxHeat, addHeat);
    float heatFrac = s->maxHeat > 0 ? (float)s->heat / s->maxHeat : 0;
    Color hc = heatFrac >= HEAT_CRITICAL ? (Color) { 255, 70, 60, (unsigned char)(140 + 115 * blink) } : heatFrac >= 0.5f
        ? (Color) { 255, 170, 80, 255 } : (Color) { 255, 170, 100, 255 };
    DrawText(heatFrac >= HEAT_CRITICAL ? "HEAT CRIT" : TextFormat("HEAT %d/%d", s->heat, s->maxHeat), x + 164, y + 38, 10, hc);
    drawReadouts(s, combatMobility(c), combatAccuracy(c), x + 8, y + 50);
    // Energy pips and status
    for (int e = 0; e < (s->maxEnergy > s->energy ? s->maxEnergy : s->energy); e++)
        DrawCircle(x + 12 + e * 11, y + 66, 4, e < s->energy ? (Color) { 60, 200, 255, 255 } : (Color) { 40, 45, 60, 255 });
    int loudest = 1;
    for (int q = 0; q < MAX_FIELD; q++) { const Combatant* o = battleField(SIDE_PLAYER, q); if (o && o != c && o->threat >= c->threat) loudest = 0; }
    DrawText(c->provoking ? "PROV" : loudest && c->threat > 0 ? "AGGRO" : "THR", x + 66, y + 61, 10,
        c->provoking ? (Color) { 255, 150, 60, 255 } : loudest && c->threat > 0 ? (Color) { 255, 110, 80, 255 } : (Color) { 170, 150, 130, 255 });
    drawThreatBar(x + 100, y + 64, 60, 4, c);
    tipText((Rectangle) { (float)x + 64, (float)y + 60, 100, 12 },
        TextFormat("THREAT %d%s", c->threat, c->provoking ? "  (PROVOKING)" : loudest && c->threat > 0 ? "  (HIGHEST ON YOUR TEAM)" : ""),
        c->provoking ? "PROVOKING: every enemy single-target attack must aim at this mech until its next turn." : threatRules());
    const char* status = c->switchLocked ? "LOCKED IN" : c->accPenalty > 0 || c->disabledWeapon >= 0 ? "SCRAMBLED"
        : c->jammed > 0 ? "JAMMED" : c->hazard > 0 ? "HAZARD" : c->slowed > 0 ? "SLOWED"
        : c->fresh && mechEffect(c->mech, CFX_STEALTH) > 0 ? "UNSEEN" : pendingText(c);
    if (status) DrawText(status, x + 166, y + 61, 10, (Color) { 200, 150, 255, 255 });
    tipText((Rectangle) { (float)x + 8, (float)y + 20, 230, 10 }, TextFormat("INTEGRITY %d/%d", s->integrity, s->maxIntegrity),
        "This mech's health. At 0 it is disabled (a recovery fee is charged) and its position stays empty until a reserve "
        "deploys there. The battle is lost when no mech is left standing.");
    tipText((Rectangle) { (float)x + 8, (float)y + 30, 230, 8 }, TextFormat("ARMOR %d/%d", s->armor, s->maxArmor),
        "Soaks the non-penetrating part of every hit before Integrity. Replated for free after each battle.");
    tipText((Rectangle) { (float)x + 8, (float)y + 38, 230, 10 }, TextFormat("HEAT %d / %d", s->heat, s->maxHeat),
        TextFormat("Every shot adds heat. A weapon that would push heat past %d can't fire (THERMAL LIMIT). At the start of "
            "your phase %d heat vents. Above 75%% you are one or two shots from locking your big weapons.", s->maxHeat, s->cooling));
    tipText((Rectangle) { (float)x, (float)y, 118, 18 }, TextFormat("%s  FW %s", m->name, firmwareLabel(m->fw.revision)),
        TextFormat("%s%s", acting ? "The mech you are commanding. Its weapons are in the panel below."
        : c->done ? "Already acted this phase (or arrived this round)." : "Click (or TAB) to command this mech.", perkLine(m)));
}

// Enemy squad header and names: the player knows what can come in, just not when
static void drawEnemyHeader(void) {
    int x = 8, y = 226;
    const Side* sd = &battle.side[SIDE_ENEMY];
    if (battle.testRange) DrawText(TextFormat("TEST RANGE   DESTROYED %d", battle.dummyKills), x, y, 12, (Color) { 255, 220, 100, 255 });
    else if (battle.trainer >= 0) {
        Trainer* t = &trainers[battle.trainer];
        DrawText(TextFormat("%s  %s", factions[t->faction].name, t->name), x, y, 12, factions[t->faction].color);
    }
    else DrawText(TextFormat("ROGUE AI  %s", factions[FAC_WILD].name), x, y, 12, (Color) { 255, 100, 100, 255 });
    DrawText(TextFormat("%d/%d LEFT", battleSideStanding(SIDE_ENEMY), sd->count), x + 182, y, 12, (Color) { 255, 200, 100, 255 });
    if (battleCanHack())
        DrawText(TextFormat("HACK TARGET %d%%", (int)roundf(battleHackChance() * 100)), x, y + 14, 10, (Color) { 120, 255, 220, 255 });
    int nx = x, ny = y + 28;
    for (int i = 0; i < sd->count; i++) {
        const Mech* m = &sd->mech[i];
        int live = battleSlotOnField(SIDE_ENEMY, i), down = !battleSlotStanding(SIDE_ENEMY, i);
        const char* label = down ? m->name : live ? TextFormat("%s", m->name) : TextFormat("%s (RSV)", m->name);
        int w = MeasureText(label, 10);
        if (nx + w > 248) { nx = x; ny += 12; }
        Color c = down ? (Color) { 110, 80, 80, 255 } : live ? (Color) { 255, 235, 235, 255 } : (Color) { 230, 140, 130, 255 };
        DrawText(label, nx, ny, 10, c);
        if (down) DrawLine(nx, ny + 5, nx + w, ny + 5, c);
        tipText((Rectangle) { (float)nx, (float)ny, (float)w, 11 }, m->name,
            down ? "Out of the fight." : live ? "On the field now." :
            "In reserve. It moves up when a position empties, and a machine below 25% Integrity pulls back if a healthy "
            "reserve can take over.");
        nx += w + 10;
    }
}

// Reactor of the mech being commanded
static void drawReactor(void) {
    const Combatant* c = battleFieldPlayer();
    const MechStats* s = &c->mech->stats;
    int spend = framePrimary >= 0 ? framePreview[framePrimary].energyCost : 0;
    int pips = s->maxEnergy > s->energy ? s->maxEnergy : s->energy;
    DrawRectangle(552, 244, 240, 52, (Color) { 15, 25, 45, 230 });
    DrawRectangleLines(552, 244, 240, 52, (Color) { 100, 200, 255, 220 });
    DrawText(TextFormat("REACTOR %s  %d/%d EN", c->mech->name, s->energy, s->maxEnergy), 560, 248, 10, (Color) { 150, 220, 255, 255 });
    int spacing = pips > 6 ? 22 : 28, r = 9;
    int x0 = 672 - (pips - 1) * spacing / 2;
    for (int i = 0; i < pips; i++) {
        int state = i >= s->energy ? 0 : (i >= s->energy - spend ? 2 : 1);
        drawEnergyPip(x0 + i * spacing, 276, r, state, i);
    }
    tipText((Rectangle) { 552, 244, 240, 52 }, TextFormat("ENERGY %d/%d", s->energy, s->maxEnergy),
        "Each field mech has its own reactor. Each weapon costs its pips; blinking pips are what the selected weapon would "
        "spend. Energy refills at the start of your phase.");
    DrawText(TextFormat("ROUND %d", battle.round), 552, 348, 14, (Color) { 150, 220, 255, 255 });
    if (c->actionsThisTurn == 0 && mechEffect(c->mech, CFX_FIRST_ACTION_FREE) > 0)
        DrawText("FIRST ACTION FREE", 640, 351, 10, (Color) { 120, 255, 180, 255 });
    const Firmware* fw = &c->mech->fw;
    int offline = 0, reversed = 0;
    for (int k = 0; k < MAX_SOCKETS; k++) {
        if (fw->chips[k] < 0 || fw->corrupt[k] <= 0) continue;
        if (fw->corruptKind[k] == CORRUPT_REVERSED) reversed++; else offline++;
    }
    const char* corrupt = "";
    if (offline) corrupt = TextFormat("%s%d OFFLINE ", corrupt, offline);
    if (reversed) corrupt = TextFormat("%s%d REVERSED ", corrupt, reversed);
    if (c->energyTax) corrupt = TextFormat("%sEN +%d ", corrupt, c->energyTax);
    if (c->randomTargeting) corrupt = TextFormat("%sRANDOM TARGETING", corrupt);
    if (corrupt[0]) {
        DrawText(TextFormat("CORRUPTED: %s", corrupt), 552, 366, 10, (Color) { 255, 110, 90, 255 });
        tipText((Rectangle) { 552, 364, 240, 14 }, "FIRMWARE CORRUPTION",
            "OFFLINE chips do nothing; REVERSED chips do the opposite (+5 Accuracy becomes -5). EN: every weapon costs "
            "1 more Energy. RANDOM TARGETING: each shot has a 50% chance to fire a random weapon. It wears off after its next turn.");
    }
}

// Six small cards: the whole team as it stands in this battle
static void drawReserveRow(void) {
    const Side* sd = &battle.side[SIDE_PLAYER];
    for (int i = 0; i < MAX_TEAM; i++) {
        Rectangle r = reserveCardRect(i);
        int x = (int)r.x, y = (int)r.y;
        DrawRectangleRec(r, (Color) { 12, 20, 36, 235 });
        if (i >= sd->count) {
            DrawRectangleLinesEx(r, 1, (Color) { 40, 55, 80, 200 });
            DrawText("--", x + 13, y + 14, 10, (Color) { 60, 75, 100, 255 });
            continue;
        }
        const Combatant* c = &sd->slot[i];
        const Mech* m = &sd->mech[i];
        const MechStats* st = &m->stats;
        int fieldPos = -1;
        for (int p = 0; p < MAX_FIELD; p++) if (sd->field[p] == i) fieldPos = p;
        int down = !battleSlotStanding(SIDE_PLAYER, i);
        Color accent = mechModel(m)->accent;
        Color edge = down ? (Color) { 200, 70, 70, 255 } : fieldPos >= 0 ? accent : (Color) { 80, 140, 200, 220 };
        if (fieldPos >= 0) DrawRectangleRec(r, (Color) { accent.r, accent.g, accent.b, 40 });
        DrawRectangleLinesEx(r, fieldPos >= 0 ? 2.0f : 1.0f, edge);
        char shortName[8];
        int k = 0;
        while (m->name[k] && m->name[k] != '-' && k < 4) { shortName[k] = m->name[k]; k++; }
        shortName[k] = 0;
        DrawText(shortName, x + 3, y + 2, 10, down ? (Color) { 150, 90, 90, 255 } : WHITE);
        drawIntegrityBar(x + 3, y + 14, 31, 4, st->integrity, st->maxIntegrity);
        drawArmorBar(x + 3, y + 19, 31, 3, st->armor, st->maxArmor);
        drawHeatBar(x + 3, y + 23, 31, 3, st->heat, st->maxHeat);
        const char* tag = down ? "DOWN" : fieldPos >= 0 ? TextFormat("POS%d", fieldPos + 1) : "RSV";
        Color tc = down ? (Color) { 255, 90, 90, 255 } : fieldPos >= 0 ? accent : (Color) { 140, 190, 240, 255 };
        DrawText(tag, x + 18 - MeasureText(tag, 10) / 2, y + 28, 10, tc);
        if (down) DrawLine(x + 2, y + 2, x + 35, y + 38, (Color) { 200, 70, 70, 160 });
        if (pendingText(c) && !down) DrawCircle(x + 32, y + 6, 3, (Color) { 200, 150, 255, 255 });
        tipText(r, TextFormat("%s  FW %s%s", m->name, firmwareLabel(m->fw.revision), fieldPos >= 0 ? "  (ON FIELD)" : down ? "  (DISABLED)" : "  (RESERVE)"),
            TextFormat("%s %s. INT %d/%d  ARM %d/%d  HEAT %d/%d.%s%s", mechModel(m)->name, roleName(mechRole(m)),
                st->integrity, st->maxIntegrity, st->armor, st->maxArmor, st->heat, st->maxHeat,
                down ? " Out of this fight; recovered after the battle." : fieldPos >= 0 ? "" :
                " Click it (or [S]) to swap it in for the mech you command, or [D] to deploy it into an empty position.",
                pendingText(c) && !down ? " Scramble effects are queued and hit on its next turn on the field." : ""));
    }
}

// ============ COMBAT LINKS (HUD) ============
static int fieldPosOfSlot(int side, int slot) {
    for (int p = 0; p < MAX_FIELD; p++) if (battle.side[side].field[p] == slot) return p;
    return -1;
}

static Vector2 bezier(Vector2 a, Vector2 c, Vector2 b, float t) {
    float u = 1 - t;
    return (Vector2) { u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y };
}

// A glowing arc along the ground between linked mechs, a mote running from the
// initiator to its partner, and the link's tag at the low point
static void drawLinkLines(void) {
    for (int side = 0; side < 2; side++) {
        ActiveLink L[MAX_TEAM];
        int n = battleLinks(side, L, MAX_TEAM);
        for (int k = 0; k < n; k++) {
            int fp = fieldPosOfSlot(side, L[k].from), tp = fieldPosOfSlot(side, L[k].to);
            if (fp < 0 || tp < 0) continue;
            Color c = linkDefs[L[k].type].color;
            Vector2 a = slotPos(side, fp), b = slotPos(side, tp);
            a.y += 40; b.y += 40;
            Vector2 mid = { (a.x + b.x) / 2, (a.y > b.y ? a.y : b.y) + 18 };
            Vector2 prev = a;
            for (int s = 1; s <= 16; s++) {
                Vector2 q = bezier(a, mid, b, s / 16.0f);
                DrawLineEx(prev, q, 5, (Color) { c.r, c.g, c.b, 50 });
                DrawLineEx(prev, q, 2, (Color) { c.r, c.g, c.b, 210 });
                prev = q;
            }
            float t = fmodf(glowTimer * 0.7f + k * 0.37f, 1.0f);
            DrawCircleV(bezier(a, mid, b, t), 3, WHITE);
            Vector2 lo = bezier(a, mid, b, 0.5f);
            const char* tag = linkDefs[L[k].type].tag;
            int tw = MeasureText(tag, 10);
            DrawRectangle((int)lo.x - tw / 2 - 3, (int)lo.y - 6, tw + 6, 12, (Color) { 10, 14, 26, 230 });
            DrawRectangleLines((int)lo.x - tw / 2 - 3, (int)lo.y - 6, tw + 6, 12, c);
            DrawText(tag, (int)lo.x - tw / 2, (int)lo.y - 5, 10, c);
        }
    }
}

// A diamond over every mech a Catcher or Scout has marked for its partner
static void drawLinkMarks(void) {
    int stacked[2][MAX_FIELD] = { 0 };
    for (int side = 0; side < 2; side++) {
        ActiveLink L[MAX_TEAM];
        int n = battleLinks(side, L, MAX_TEAM);
        for (int k = 0; k < n; k++) {
            if (L[k].type != LINK_TARGETING && L[k].type != LINK_SPOTTER) continue;
            int mp = battleMarkPos(side, L[k].from);
            if (mp < 0) continue;
            Color c = linkDefs[L[k].type].color;
            Vector2 v = slotPos(1 - side, mp);
            float pulse = 0.5f + 0.5f * sinf(glowTimer * 5);
            Vector2 at = { v.x + 30, v.y - 46 + 14.0f * stacked[1 - side][mp]++ };
            DrawPoly(at, 4, 6 + pulse, 0, (Color) { c.r, c.g, c.b, 200 });
            DrawPolyLines(at, 4, 8 + pulse, 0, WHITE);
            DrawText("MARK", (int)at.x + 10, (int)at.y - 5, 10, c);
        }
    }
}

// Static over an enemy the commanded mech can't see (Signal Blackout)
static void drawNoSignal(Rectangle r) {
    for (int k = 0; k < 8; k++) {
        int y = (int)r.y + GetRandomValue(0, (int)r.height);
        DrawLine((int)r.x, y, (int)(r.x + r.width), y, (Color) { 200, 150, 255, 120 });
    }
    const char* t = "NO SIGNAL";
    DrawText(t, (int)(r.x + r.width / 2 - MeasureText(t, 10) / 2), (int)(r.y + r.height / 2), 10, (Color) { 220, 180, 255, 255 });
}

// What a link does right now, in one line
static const char* linkSummary(int side, const ActiveLink* l) {
    const Side* sd = &battle.side[side];
    const LinkDef* L = &linkDefs[l->type];
    float k = battleLinkScale(side, l->from);   // Link Boost
    int mp = battleMarkPos(side, l->from);
    const char* mark = mp >= 0 ? battleField(1 - side, mp)->mech->name : NULL;
    switch (l->type) {
    case LINK_TARGETING:
        return mark ? TextFormat("+%d ACC, %d%% CRIT vs %s", (int)(L->value * k), (int)roundf(L->value2 * k * 100), mark)
                    : "no mark yet - it marks what it shoots";
    case LINK_SPOTTER:
        return mark ? TextFormat("+%d ACC, reaches %s past cover", (int)(L->value * k), mark) : "no mark yet - it marks what it shoots";
    case LINK_DEFENSE:
        return TextFormat("takes %d%% of every hit on %s", (int)roundf(L->value * k * 100), sd->mech[l->to].name);
    default: {
        int jammed = 0;
        for (int p = 0; p < MAX_FIELD; p++) {
            const Combatant* e = battleField(1 - side, p);
            if (e && e->jammed > 0 && e->jammedBy == l->from) jammed++;
        }
        return jammed ? TextFormat("%d jammed: can't target %s", jammed, sd->mech[l->to].name) : "jam an enemy to blind it to the Recon";
    }
    }
}

// Left column under the enemy squad: every active link, yours first
static void drawLinkPanel(void) {
    int x = 8, y = LINK_PANEL_Y;
    DrawText("COMBAT LINKS", x, y + 2, 10, (Color) { 150, 220, 255, 255 });
    tipText((Rectangle) { (float)x, (float)y, 120, 12 }, "COMBAT LINKS",
        "Pairs of allies on the field. Catcher > Artillery: TARGETING. Aegis > Heavy Assault: DEFENSE. Scout > Artillery: SPOTTER. "
        "Disruptor > Recon: SIGNAL BLACKOUT. Links form at the start and when a mech takes the field, and break below 25% Integrity, "
        "on a switch or when a mech goes down. LINK [K] (1 EN) re-links an initiator.");
    if (battle.phase == BP_PLAYER_TURN && battleActing()) {
        const char* noLink = NULL;
        int canLink = battleCanLink(&noLink), cand = battleLinkCandidate();
        drawButton(linkButtonRect(), "LINK [K]", 10, 0, canLink);
        tipText(linkButtonRect(), "LINK", canLink
            ? TextFormat("%d EN, uses this mech's action: link to %s (%s). Replaces its current link.", battleLinkCost(battleActing()),
                battle.side[SIDE_PLAYER].mech[cand].name, linkDefs[battleLinkInitiator(battleActing())].name)
            : TextFormat("Can't link: %s.", noLink ? noLink : "-"));
    }
    int ly = y + 16, shown = 0, total = 0;
    for (int pass = 0; pass < 2; pass++) {
        int side = pass == 0 ? SIDE_PLAYER : SIDE_ENEMY;
        ActiveLink L[MAX_TEAM];
        int n = battleLinks(side, L, MAX_TEAM);
        for (int k = 0; k < n; k++, total++) {
            if (shown >= 3) continue;
            const LinkDef* d = &linkDefs[L[k].type];
            const Side* sd = &battle.side[side];
            Color c = side == SIDE_PLAYER ? d->color : (Color) { 255, 140, 130, 255 };
            DrawRectangle(x, ly, 3, 21, d->color);
            DrawText(TextFormat("%s%s > %s", side == SIDE_ENEMY ? "ENEMY " : "", sd->mech[L[k].from].name, sd->mech[L[k].to].name),
                x + 7, ly, 10, c);
            DrawText(d->tag, x + 238 - MeasureText(d->tag, 10), ly, 10, d->color);
            DrawText(linkSummary(side, &L[k]), x + 7, ly + 11, 10, (Color) { 175, 190, 215, 255 });
            tipText((Rectangle) { (float)x, (float)ly, 240, 22 }, TextFormat("%s%s", side == SIDE_ENEMY ? "ENEMY " : "", d->name), d->rule);
            ly += 25;
            shown++;
        }
    }
    if (total > shown) DrawText(TextFormat("+%d more", total - shown), x + 7, ly - 2, 10, (Color) { 130, 150, 175, 255 });
    if (total == 0)
        wrapText("None active. Catcher or Scout + Artillery, Aegis + Heavy Assault and Disruptor + Recon link up.",
            x, ly, 236, 10, (Color) { 120, 140, 170, 255 }, 1);
}

static void drawReticle(Rectangle r, Color rc) {
    int L = 12;
    DrawLineEx((Vector2) { r.x, r.y }, (Vector2) { r.x + L, r.y }, 2, rc);
    DrawLineEx((Vector2) { r.x, r.y }, (Vector2) { r.x, r.y + L }, 2, rc);
    DrawLineEx((Vector2) { r.x + r.width, r.y }, (Vector2) { r.x + r.width - L, r.y }, 2, rc);
    DrawLineEx((Vector2) { r.x + r.width, r.y }, (Vector2) { r.x + r.width, r.y + L }, 2, rc);
    DrawLineEx((Vector2) { r.x, r.y + r.height }, (Vector2) { r.x + L, r.y + r.height }, 2, rc);
    DrawLineEx((Vector2) { r.x, r.y + r.height }, (Vector2) { r.x, r.y + r.height - L }, 2, rc);
    DrawLineEx((Vector2) { r.x + r.width, r.y + r.height }, (Vector2) { r.x + r.width - L, r.y + r.height }, 2, rc);
    DrawLineEx((Vector2) { r.x + r.width, r.y + r.height }, (Vector2) { r.x + r.width, r.y + r.height - L }, 2, rc);
}

// Faint ground lines for each side's Front and Rear rows (the HUD badges name them)
static void drawLaneRows(void) {
    static const int rows[4] = { ENEMY_REAR_Y, ENEMY_FRONT_Y, PLAYER_FRONT_Y, PLAYER_REAR_Y };
    for (int i = 0; i < 4; i++) {
        int y = rows[i] + 36;
        Color c = i == 0 || i == 3 ? (Color) { 90, 140, 120, 90 } : (Color) { 140, 150, 180, 90 };
        for (int x = 276; x < 526; x += 12) DrawLine(x, y, x + 6, y, c);
    }
}

// The machines on the field: target reticle on the enemy you aim at, a glow
// under the mech you command, dashed markers on empty positions; flankers get
// chevrons, and a covered target points at the guard that takes the shot
static void drawField(float sx, float sy) {
    float blink = 0.5f + 0.5f * sinf(glowTimer * 6);
    drawLaneRows();
    drawLinkLines();
    for (int side = 0; side < 2; side++)
        for (int p = 0; p < MAX_FIELD; p++) {
            if (p >= battle.side[side].numField) continue;
            Vector2 v = slotPos(side, p);
            const Combatant* c = battleField(side, p);
            if (!c) { DrawEllipseLines((int)v.x, (int)v.y + 36, 30, 8, (Color) { 70, 90, 120, 200 }); continue; }
            int acting = side == SIDE_PLAYER && battle.phase == BP_PLAYER_TURN && p == battle.actingSlot && battleActing() == c;
            if (acting) {
                Color a = mechModel(c->mech)->accent;
                DrawEllipse((int)v.x, (int)v.y + 36, 36, 10, (Color) { a.r, a.g, a.b, (unsigned char)(60 + 60 * blink) });
                DrawEllipseLines((int)v.x, (int)v.y + 36, 36, 10, a);
            }
            if (c->flanking) {   // chevrons either side, pointing at the other side
                int dir = side == SIDE_PLAYER ? -1 : 1;
                Color fc = { 255, 160, 70, (unsigned char)(120 + 100 * blink) };
                for (int s2 = -1; s2 <= 1; s2 += 2)
                    for (int k = 0; k < 2; k++) {
                        float cx = v.x + s2 * 34, cy = v.y + dir * (k * 8 - 4);
                        DrawLineEx((Vector2) { cx - 6, cy - dir * 4 }, (Vector2) { cx, cy }, 2, fc);
                        DrawLineEx((Vector2) { cx + 6, cy - dir * 4 }, (Vector2) { cx, cy }, 2, fc);
                    }
            }
            drawMechBattle(c->mech->model, (int)(v.x + sx), (int)(v.y + sy), SPRITE_SCALE, side == SIDE_ENEMY);
            if (side == SIDE_ENEMY && battle.phase == BP_PLAYER_TURN && battleHidden(p)) drawNoSignal(spriteRect(side, p));
            drawThreatBar((int)v.x - 25, (int)v.y + 48, 50, 4, c);
            if (c->provoking) {
                float pb = 0.5f + 0.5f * sinf(glowTimer * 8);
                DrawCircleLines((int)v.x, (int)v.y, 40 + 3 * pb, (Color) { 255, 150, 60, (unsigned char)(120 + 100 * pb) });
                DrawText("PROVOKING", (int)v.x - MeasureText("PROVOKING", 10) / 2, (int)v.y + 54, 10, (Color) { 255, 150, 60, 255 });
            }
            else if (c->flanking)
                DrawText("FLANK", (int)v.x - MeasureText("FLANK", 10) / 2, (int)v.y + 54, 10, (Color) { 255, 160, 70, 255 });
            else if (side == SIDE_PLAYER && c->done && !acting && battle.phase == BP_PLAYER_TURN)
                DrawText("DONE", (int)v.x - MeasureText("DONE", 10) / 2, (int)v.y + 54, 10, (Color) { 130, 140, 160, 255 });
            if (side == SIDE_ENEMY && framePreviewOk[p]) {   // reticle where the shot lands, a lighter one on splash targets
                int primary = p == framePrimary;
                drawReticle(spriteRect(side, p), primary ? (Color) { 255, 220, 80, (unsigned char)(160 + 95 * blink) } : (Color) { 255, 150, 80, 150 });
            }
            if (side == SIDE_ENEMY && p == battle.playerTarget && intercepted()) {   // aimed here, but the guard steps in
                Vector2 g = slotPos(SIDE_ENEMY, framePrimary);
                Color cc = { 120, 220, 255, (unsigned char)(120 + 100 * blink) };
                drawReticle(spriteRect(side, p), (Color) { 120, 150, 190, 150 });
                Vector2 from = { v.x + (g.x > v.x ? 30.0f : -30.0f), v.y + 18 }, to = { g.x + (g.x > v.x ? -30.0f : 30.0f), g.y };
                DrawLineEx(from, to, 2, cc);
                DrawCircleV(to, 3, cc);
                DrawText("COVERED", (int)v.x - MeasureText("COVERED", 10) / 2, (int)v.y - 56, 10, (Color) { 120, 220, 255, 255 });
            }
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
    if (w->ammo > 0 && usable)   // a blocked weapon shows its reason there instead
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
    const MechStats* ds = &previewVictim()->mech->stats;
    int hitPct = (int)roundf(p->hitChance * 100);
    DrawText(TextFormat("%s > %s", w->name, previewVictim()->mech->name), x, y, 12, munitionColor(w->munition));
    if (intercepted())
        DrawText(TextFormat("COVERING %s", battleFieldEnemy()->mech->name), x + 20 + MeasureText(TextFormat("%s > %s", w->name,
            previewVictim()->mech->name), 12), y + 1, 10, (Color) { 120, 220, 255, 255 });
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
    int splash = 0;
    for (int q = 0; q < MAX_FIELD; q++) splash += framePreviewOk[q] && q != framePrimary;
    if (splash) DrawText(TextFormat("%s: also hits %d more (-25%% each).", targetingNames[battleTargetingOf(weaponSel)], splash), x + 130, ly, 10,
        (Color) { 255, 170, 90, 255 });
    ly += 13;
    int spotted = !intercepted() && framePrimary >= 0 && battleInterceptor(SIDE_ENEMY, framePrimary) >= 0
        && (battleTargetingOf(weaponSel) == TARGET_SINGLE || battleTargetingOf(weaponSel) == TARGET_LINE);
    if (p->linkAccuracy > 0 || p->critChance > 0)
        DrawText(TextFormat("LINKED: +%d Accuracy vs the mark%s.", p->linkAccuracy,
            p->critChance > 0 ? TextFormat(", %d%% crit for x%.1f", (int)roundf(p->critChance * 100), CRIT_MULT)
            : spotted ? ", spotted past its cover" : ""), x, ly, 10, (Color) { 120, 255, 170, 255 });
    else if (p->guardMod < 1)
        DrawText(TextFormat("DEFENSE LINK: their Aegis takes %d%% of this hit.", (int)roundf((1 - p->guardMod) * 100)), x, ly, 10,
            (Color) { 130, 200, 255, 255 });
    DrawText("[I] why   [V] formula   [L] log   hover anything", x, y + 122, 10, (Color) { 100, 200, 240, 255 });
}

// Every step of the attack formula for the selected weapon, before it is fired
static void drawPreviewPanel(const AttackPreview* p, const Weapon* w, int x, int y) {
    Color txt = { 210, 225, 240, 255 }, dim = { 130, 150, 175, 255 }, res = { 120, 255, 160, 255 };
    DrawText(TextFormat("PREVIEW  %s > %s%s", w->name, previewVictim()->mech->name, intercepted() ? " (COVERING)" : ""), x, y, 12,
        munitionColor(w->munition));
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

// Switch / deploy picker: every standing reserve with what it brings in
static void drawPicker(void) {
    int deploying = pickerDeploy, forced = pickerDeploy && battle.phase == BP_DEPLOY;
    int slots[MAX_TEAM + 1], n = pickerRows(slots);
    int top = pickerTop(n), h = n * PICKER_ROW + 80;
    const Combatant* out = battleFieldPlayer();
    DrawRectangle((int)-layoutX(), 0, screenW, SCREEN_H, (Color) { 0, 0, 0, 120 });
    DrawRectangle(PICKER_X, top, PICKER_W, h, (Color) { 8, 14, 28, 255 });
    DrawRectangleLines(PICKER_X, top, PICKER_W, h, deploying ? (Color) { 255, 110, 100, 255 } : (Color) { 120, 220, 255, 255 });
    DrawText(forced ? "NO MECH ON THE FIELD - DEPLOY A RESERVE" : deploying ? "DEPLOY A RESERVE TO AN EMPTY POSITION"
        : TextFormat("SWITCH OUT %s", out->mech->name), PICKER_X + 12, top + 10, 18,
        forced ? (Color) { 255, 140, 120, 255 } : (Color) { 150, 230, 255, 255 });
    for (int i = 0; i < n; i++) {
        Rectangle r = pickerRowRect(i, n);
        int x = (int)r.x, y = (int)r.y, sel = i == pickerSel;
        DrawRectangleRec(r, sel ? (Color) { 30, 70, 110, 255 } : (Color) { 16, 26, 44, 255 });
        DrawRectangleLinesEx(r, sel ? 2.0f : 1.0f, sel ? (Color) { 120, 240, 255, 255 } : (Color) { 60, 100, 150, 220 });
        if (slots[i] < 0) {
            DrawText("YIELD - withdraw and lose the battle", x + 12, y + 14, 14, (Color) { 255, 130, 120, 255 });
            continue;
        }
        const Combatant* c = &battle.side[SIDE_PLAYER].slot[slots[i]];
        const Mech* m = c->mech;
        const MechStats* st = &m->stats;
        DrawText(m->name, x + 10, y + 4, 16, mechModel(m)->accent);
        DrawText(TextFormat("%s %s  FW %s", mechModel(m)->name, roleName(mechRole(m)), firmwareLabel(m->fw.revision)),
            x + 10, y + 24, 10, (Color) { 180, 200, 230, 255 });
        drawIntegrityBar(x + 200, y + 6, 150, 7, st->integrity, st->maxIntegrity);
        DrawText(TextFormat("INT %d/%d", st->integrity, st->maxIntegrity), x + 358, y + 4, 10, WHITE);
        drawArmorBar(x + 200, y + 17, 150, 6, st->armor, st->maxArmor);
        DrawText(TextFormat("ARM %d/%d", st->armor, st->maxArmor), x + 358, y + 15, 10, (Color) { 150, 190, 240, 255 });
        drawHeatBar(x + 200, y + 27, 150, 6, st->heat, st->maxHeat);
        DrawText(TextFormat("HEAT %d/%d", st->heat, st->maxHeat), x + 358, y + 26, 10, (Color) { 255, 170, 100, 255 });
        int pending = c->nextSkipTurn + (c->nextDisabledWeapon >= 0) + (c->nextAccPenalty > 0) + c->nextEnergyLoss
            + c->nextEnergyTax + c->nextRandomTargeting;
        if (pending) DrawText(TextFormat("%d SCRAMBLE%s QUEUED", pending, pending > 1 ? "S" : ""), x + 440, y + 28, 10,
            (Color) { 200, 150, 255, 255 });
        DrawText(TextFormat("MOB %d  ACC %d", st->mobility, st->accuracy), x + 440, y + 4, 10, (Color) { 170, 200, 230, 255 });
        DrawText(TextFormat("STB %d  PWR %.2f", st->stability, st->power), x + 440, y + 16, 10, (Color) { 170, 200, 230, 255 });
    }
    const char* rule = deploying
        ? "Free: no Energy cost and no lockout. It takes the empty position and acts from next round."
        : TextFormat("Costs %d EN - %s's remaining %d EN is lost. The new mech acts from next round.", SWITCH_ENERGY_COST,
            out->mech->name, out->mech->stats.energy);
    DrawText(rule, PICKER_X + 12, top + h - 36, 10, (Color) { 255, 220, 120, 255 });
    DrawText(deploying ? (forced ? "Damage, Heat and queued scrambles stay with each mech.   [W/S] select   [Z/CLICK] deploy"
                                 : "Damage, Heat and queued scrambles stay with each mech.   [Z/CLICK] deploy   [ESC] not now")
                       : "The new mech can't switch out on its next turn.   [W/S] select   [Z/CLICK] switch   [ESC] cancel",
        PICKER_X + 12, top + h - 22, 10, (Color) { 120, 200, 240, 255 });
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
    computeFramePreviews();
    tip.active = 0;

    BeginMode2D(layoutCamera());
    drawField(sx, sy);
    drawLinkMarks();
    drawEffects();
    drawParticles();
    drawDamageNums();

    for (int pos = 0; pos < MAX_FIELD; pos++) {
        if (pos < battle.side[SIDE_ENEMY].numField) drawEnemySlot(pos);
        if (pos < battle.side[SIDE_PLAYER].numField) drawPlayerSlot(pos);
    }
    drawEnemyHeader();
    drawLinkPanel();
    drawReactor();
    drawReserveRow();

    // Log strip and bottom panel
    DrawRectangle(20, PANEL_Y - 22, SCREEN_W - 40, 20, (Color) { 10, 15, 30, 200 });
    DrawText(battle.log, 30, PANEL_Y - 18, 12, (Color) { 190, 220, 240, 255 });
    DrawText("[L] LOG", SCREEN_W - 70, PANEL_Y - 17, 10, (Color) { 100, 200, 240, 255 });
    const LogEntry* latest = battleLogEntry(0);
    if (latest && latest->why.n > 0) tipWhy(logStripRect(), "LAST ACTION", latest->text, &latest->why);
    DrawRectangle(20, PANEL_Y, SCREEN_W - 40, 172, (Color) { 15, 20, 35, 240 });
    DrawRectangleLines(20, PANEL_Y, SCREEN_W - 40, 172, (Color) { 80, 220, 255, 200 });

    const Combatant* actor = battleActing();
    if ((battle.phase == BP_PLAYER_TURN || battle.phase == BP_DEPLOY) && battle.dialogue == DLG_NONE) {
        if (actor) {
            int ready = 0;
            for (int q = 0; q < MAX_FIELD; q++) { const Combatant* c = battleField(SIDE_PLAYER, q); ready += c && !c->done; }
            DrawText(TextFormat(">> %s", actor->mech->name), 30, PANEL_Y + 6, 14, mechModel(actor->mech)->accent);
            DrawText(TextFormat("%d TO ACT", ready), 30 + MeasureText(TextFormat(">> %s", actor->mech->name), 14) + 8, PANEL_Y + 9, 10,
                (Color) { 120, 200, 240, 255 });
            for (int i = 0; i < MAX_WEAPONS; i++) drawWeaponButton(i);
        }
        else {
            DrawText(battle.phase == BP_DEPLOY ? ">> NO MECH ON THE FIELD" : ">> ALL MECHS HAVE ACTED", 30, PANEL_Y + 6, 14, (Color) { 100, 240, 255, 255 });
            DrawText(battle.phase == BP_DEPLOY ? "Deploy a reserve [D] - or yield from the deploy list."
                : "Deploy a reserve [D] into an empty position, or end the phase [X].", 35, PANEL_Y + 40, 14, (Color) { 200, 220, 240, 255 });
        }
        const char* noSwitch = NULL;
        int canSwitch = battleCanSwitch(&noSwitch) || (noSwitch && strcmp(noSwitch, "STANDBY") == 0);
        drawButton(deployButtonRect(), "DEPLOY [D]", 12, 0, battleCanDeploy());
        drawButton(switchButtonRect(), "SWITCH [S]", 12, 0, canSwitch);
        const char* noProvoke = NULL;
        int canProvoke = battleCanProvoke(&noProvoke);
        drawButton(provokeButtonRect(), "PROVOKE [P]", 12, 0, canProvoke);
        tipText(provokeButtonRect(), "PROVOKE", canProvoke
            ? TextFormat("%d EN: Threat +%d, and every enemy single-target attack must aim at this mech until its next turn. "
                "Area and cone weapons still hit everyone. Use it to pull fire off a damaged teammate.", battleProvokeCost(battleActing()), THREAT_PROVOKE)
            : TextFormat("Can't provoke: %s. Needs the PROVOCATION PROTOCOL chip (built for Ironclads; REDOUBT has it built in).",
                noProvoke ? noProvoke : "-"));
        drawButton(hackButtonRect(), "HACK [C]", 12, 0, battleCanHack());
        if (actor) {   // formation, under the reactor
            int toRear = actor->lane == LANE_FRONT;
            const char *noLane = NULL, *noFlank = NULL;
            int canLane = battleCanMove(toRear ? LANE_REAR : LANE_FRONT, &noLane);
            int canFlank = battleCanMove(MOVE_FLANK, &noFlank);
            const char* cost = battleMoveCost(actor) ? TextFormat("%d EN", MOVE_ENERGY_COST) : "free";
            drawButton(laneButtonRect(), TextFormat("%s [F]", toRear ? "TO REAR" : "TO FRONT"), 10, 0, canLane);
            drawButton(flankButtonRect(), "FLANK [G]", 10, 0, canFlank);
            tipText(laneButtonRect(), toRear ? "MOVE TO THE REAR" : "MOVE TO THE FRONT", canLane
                ? TextFormat("%s (Recon and EW move free), once a turn - the mech can still act. %s", cost, toRear
                    ? "REAR: a Front ally takes single-target and line attacks aimed at it; area and cone weapons still reach it."
                    : "FRONT: it takes single-target and line attacks aimed at its Rear allies.")
                : TextFormat("Can't move: %s.", noLane ? noLane : "-"));
            tipText(flankButtonRect(), "FLANK", canFlank
                ? TextFormat("Free for Recon / EW, once a turn - the mech can still act. Until its next turn its attacks ignore %d%% "
                    "of the target's Armor, but it is at -%d Mobility and out of formation: it doesn't cover the Rear and nothing "
                    "covers it.", (int)roundf(FLANK_ARMOR_IGNORE * 100), FLANK_MOBILITY_PENALTY)
                : TextFormat("Can't flank: %s.", noFlank ? noFlank : "-"));
        }
        drawButton(nextButtonRect(), "NEXT [TAB]", 12, 0, actor != NULL);
        drawButton(endTurnButtonRect(), "END TURN [X]", 12, 0, battleFieldCount(SIDE_PLAYER) > 0);
        tipText(deployButtonRect(), "DEPLOY", battleCanDeploy()
            ? "Free: a reserve takes an empty position. It doesn't act until next round."
            : "Needs an empty position (a disabled mech leaves one) and a standing reserve.");
        tipText(switchButtonRect(), "SWITCH", canSwitch
            ? TextFormat("Swap the mech you command for a reserve. Costs %d EN (the rest of its Energy is lost) and is its action. "
                "The incoming mech acts next round and can't switch out on its next turn. Damage, Heat and queued scrambles stay "
                "with each mech.", SWITCH_ENERGY_COST)
            : TextFormat("Can't switch now: %s.", noSwitch ? noSwitch : "-"));
        tipText(nextButtonRect(), "NEXT MECH", "Command the next field mech that hasn't acted. You can also click a mech or its panel.");
        tipText(endTurnButtonRect(), "END TURN", "End your phase: mechs that haven't acted keep their Energy unused. Then the enemy phase.");
        tipText(hackButtonRect(), "HACK (REPROGRAM)", battleCanHack()
            ? TextFormat("Chance %d%% = your hack Strength %d / (Strength + their Stability %d). Scramble them first: every "
                "pending scramble or corruption lowers their Stability by %d. Uses this mech's action.",
                (int)roundf(battleHackChance() * 100), hackStrength(battleFieldPlayer()->mech), battleHackStability(), HACK_STABILITY_PER_EFFECT)
            : "Only unmanned rogue AI machines can be reprogrammed, and your team needs a free slot.");
        if (actor) {
            DrawLine(393, ROW_Y, 393, ROW_Y + 134, (Color) { 50, 90, 130, 200 });
            if (p && formulaView) drawPreviewPanel(p, mechWeapon(actor->mech, weaponSel), 402, ROW_Y);
            else if (p) drawPlainPreview(p, mechWeapon(actor->mech, weaponSel), 402, ROW_Y);
            else if (!battleBusy()) DrawText(battleTarget() ? "No weapon on this mount." : "No target.", 402, ROW_Y, 12, (Color) { 130, 150, 175, 255 });
        }
        DrawText(battle.testRange ? "[Z] FIRE [ARROWS] WEAPON [Q/E] TARGET [TAB] NEXT [S] SWITCH [X] END [R] NEW DUMMY [ESC] LEAVE"
                                  : "[Z] FIRE [ARROWS] WPN [Q/E] TARGET [TAB] NEXT [S] SWITCH [D] DEPLOY [F] LANE [G] FLANK [K] LINK [P] PROVOKE [C] HACK [X] END",
            30, ROW_Y + 134, 10, (Color) { 100, 240, 255, 255 });
    }
    else {
        DrawText(battle.phase == BP_ENEMY_TURN ? ">> ENEMY PHASE <<" : ">> SYS LOG <<", 30, PANEL_Y + 6, 14, (Color) { 100, 240, 255, 255 });
        wrapText(battle.log, 35, PANEL_Y + 30, SCREEN_W - 90, 16, (Color) { 220, 240, 255, 255 }, 1);

        if (battle.phase == BP_VICTORY && battle.dialogue == DLG_NONE && !battleBusy()) {
            if (battle.hacked && battle.trainer < 0)
                DrawText(">> HOSTILES CLEARED! [Z/CLICK] <<", 35, PANEL_Y + 90, 20, (Color) { 120, 255, 220, 255 });
            else if (battle.trainer >= 0)
                DrawText(">> VICTORY! [Z/CLICK] TO CONTINUE <<", 35, PANEL_Y + 90, 20, (Color) { 255, 220, 100, 255 });
            else
                DrawText(">> HOSTILES SCRAPPED! [Z/CLICK] <<", 35, PANEL_Y + 90, 20, (Color) { 120, 255, 180, 255 });
            DrawText(TextFormat("+%d REVISION DATA", battle.dataEarned), 480, PANEL_Y + 90, 20, (Color) { 200, 170, 255, 255 });
        }
        if ((battle.phase == BP_VICTORY || battle.phase == BP_DEFEAT) && battle.dialogue == DLG_NONE && battle.loot[0])
            DrawText(battle.loot, 35, PANEL_Y + 62, 14, (Color) { 255, 220, 120, 255 });
        if (battle.phase == BP_DEFEAT && battle.dialogue == DLG_NONE)
            DrawText(">> TEAM DISABLED! [Z/CLICK] TO CONTINUE <<", 35, PANEL_Y + 90, 20, (Color) { 255, 100, 100, 255 });

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
    if (pickerOpen) { tip.active = 0; drawPicker(); }
    else if (logOpen) drawLogOverlay();
    else if (infoOpen && p && actor) {   // keyboard route to the same breakdown the weapon tooltip shows
        Explanation why;
        if (battleExplainPlayer(weaponSel, &why)) {
            const Weapon* w = mechWeapon(actor->mech, weaponSel);
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

    // The wipe opens over the battle, spanning the whole canvas. Drawn last
    // so it sits on top of everything (including tooltips), and OUTSIDE
    // BeginMode2D so it uses raw canvas coordinates.
    if (transition.active && transition.phase == 3) {
        transitionDraw();
    }
}

// ============ FIRMWARE REVISION SCREEN ============
static int revFrom = 0, revTo = 0, optSel = 0, branchSel = 0, revIdx = 0;
static float revTimer = 0;

static Rectangle optionRect(int i) {
    int col = i % 3, row = i / 3;
    return (Rectangle) { 130.0f + col * 185, 430.0f + row * 50, 175, 40 };
}

// Every team mech that gained revisions in the battle gets its own screen
static Mech* revMech(void) {
    if (revIdx < battle.numRevisions && battle.revTeam[revIdx] < teamSize) return &team[battle.revTeam[revIdx]];
    return rosterActive();
}

static void revisionShow(int idx) {
    revIdx = idx;
    revFrom = idx < battle.numRevisions ? battle.revFrom[idx] : revMech()->fw.revision;
    revTo = revMech()->fw.revision;
    optSel = 0;
    branchSel = 0;
    revTimer = 0;
}

void uiRevisionOpen(void) { revisionShow(0); }

void uiRevisionUpdate(float dt, GameState* state) {
    Mech* m = revMech();
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
        if (revIdx + 1 < battle.numRevisions) revisionShow(revIdx + 1);
        else *state = STATE_OVERWORLD;
    }
}

void uiRevisionDraw(void) {
    Mech* m = revMech();
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
