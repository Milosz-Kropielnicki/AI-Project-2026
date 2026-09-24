#include "ui.h"
#include "game.h"
#include "battle.h"
#include "world.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

// ============ EFFECTS ============
#define MAX_PARTICLES 512
typedef struct { Vector2 pos, vel; float life, maxLife, size; Color color; int active; } Particle;
static Particle particles[MAX_PARTICLES];

typedef struct {
    int active, kind;
    float t, duration;
    Vector2 from, to;
    Color color;
    int damageShown, damage, hit;
} Effect;
#define MAX_EFFECTS 8
static Effect effects[MAX_EFFECTS];

typedef struct { int active; Vector2 pos; float life; int damage, healing; Color color; } DamageNum;
#define MAX_DAMAGE_NUMS 16
static DamageNum damageNums[MAX_DAMAGE_NUMS];

static float shakeAmount = 0, shakeTimer = 0;
static const Vector2 playerMechPos = { 160, 300 };
static const Vector2 enemyMechPos = { 520, 150 };

static void spawnParticle(Vector2 pos, Vector2 vel, float life, float size, Color c) {
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!particles[i].active) { particles[i] = (Particle){ pos, vel, life, life, size, c, 1 }; return; }
}

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

static void addEffect(int kind, Vector2 from, Vector2 to, Color color, int damage, int hit) {
    for (int i = 0; i < MAX_EFFECTS; i++)
        if (!effects[i].active) {
            effects[i] = (Effect){ 1, kind, 0, effectDuration(kind), from, to, color, 0, damage, hit };
            return;
        }
}

static void addDamageNum(Vector2 pos, int damage, int miss, int healing) {
    for (int i = 0; i < MAX_DAMAGE_NUMS; i++)
        if (!damageNums[i].active) {
            damageNums[i] = (DamageNum){ 1, pos, 1.2f, damage, healing,
                healing ? (Color) { 120,255,160,255 } : miss ? (Color) { 200,200,200,255 } : (Color) { 255,90,90,255 } };
            return;
        }
}

static void shakeScreen(float amount, float time) {
    if (amount > shakeAmount) { shakeAmount = amount; shakeTimer = time; }
}

// Impact: burst + damage number + shake, or a MISS marker
static void impact(Effect* e, int burst, float smin, float smax, float life, float shake, float shakeTime, float missOffset) {
    if (e->hit) {
        if (burst > 0) spawnBurst(e->to, burst, e->color, smin, smax, life);
        addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, e->damage, 0, 0);
        shakeScreen(shake, shakeTime);
    }
    else addDamageNum((Vector2) { e->to.x + missOffset, e->to.y - 40 }, 0, 1, 0);
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
        p->vel.y += 60 * dt;
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
                if (!e->hit) addDamageNum((Vector2) { e->to.x, e->to.y - 40 }, 0, 1, 0);
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
        const char* txt = d->healing ? TextFormat("+%d", d->damage) : d->damage == 0 ? "MISS" : TextFormat("%d", d->damage);
        int sz = d->healing ? 24 : 28;
        int w = MeasureText(txt, sz);
        DrawText(txt, (int)(d->pos.x - w / 2 + 2), (int)d->pos.y + 2, sz, (Color) { 0, 0, 0, c.a });
        DrawText(txt, (int)(d->pos.x - w / 2), (int)d->pos.y, sz, c);
    }
}

// ============ BATTLE SCREEN ============
static int weaponSel = 0;
static int reselectAfterShot = 0;

// Layout: HUDs on top, log strip, then a bottom panel with the weapon list on
// the left and the formula preview for the selected weapon on the right.
#define PANEL_Y (SCREEN_H - 180)
#define ROW_Y (PANEL_Y + 26)

static Rectangle weaponButtonRect(int i) { return (Rectangle) { 30, (float)(ROW_Y + i * 34), 355, 30 }; }
static Rectangle hackButtonRect(void) { return (Rectangle) { 470, PANEL_Y + 4, 120, 18 }; }
static Rectangle endTurnButtonRect(void) { return (Rectangle) { 600, PANEL_Y + 4, 170, 18 }; }

void uiBattleOpen(void) {
    weaponSel = 0;
    reselectAfterShot = 0;
    clearEffects();
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
        addEffect(ev.fx, from, to, ev.color, ev.damage, ev.hit);
    }

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

    if (DOWN_PRESSED || RIGHT_PRESSED) weaponSel = (weaponSel + 1) % MAX_WEAPONS;
    if (UP_PRESSED || LEFT_PRESSED)    weaponSel = (weaponSel + MAX_WEAPONS - 1) % MAX_WEAPONS;

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
            const Weapon* w = mechWeapon(battle.player.mech, weaponSel);
            snprintf(battle.log, sizeof(battle.log), "%s: %s", w ? w->name : "MOUNT", reason);
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
}

static int previewSelected(AttackPreview* p) {
    return battle.phase == BP_PLAYER_TURN && battle.dialogue == DLG_NONE && battlePreviewPlayer(weaponSel, p);
}

static void drawEnemyHud(const AttackPreview* p) {
    const Mech* m = &battle.enemyMech;
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
    const char* arch = battle.enemyArchetype >= 0 ? TextFormat("  [%s%s]", archetypes[battle.enemyArchetype].boss ? "BOSS " : "",
        archetypes[battle.enemyArchetype].name) : "";
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
    drawReadouts(s, combatMobility(&battle.enemy), combatAccuracy(&battle.enemy), 30, 132);
}

static void drawPlayerHud(const AttackPreview* p) {
    const Combatant* c = &battle.player;
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
    drawReadouts(s, combatMobility(c), combatAccuracy(c), x + 10, y + 128);
    int need = firmwareDataToNext(m->fw.revision);
    drawDataBar(x + 10, y + 150, 250, 5, m->fw.data, need);
    DrawText(TextFormat("DATA %d/%d", m->fw.data, need), x + 268, y + 147, 10, (Color) { 200, 170, 255, 255 });
    if (c->accPenalty > 0 || c->disabledWeapon >= 0)
        DrawText(TextFormat("SCRAMBLED%s%s", c->accPenalty > 0 ? TextFormat(" ACC-%d", c->accPenalty) : "",
            c->disabledWeapon >= 0 ? " WPN OFFLINE" : ""), x + 10, y + 160, 10, (Color) { 200, 150, 255, 255 });
}

static void drawReactor(const AttackPreview* p) {
    const MechStats* s = &battle.player.mech->stats;
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
    if (battle.player.actionsThisTurn == 0 && firmwareEffect(&battle.player.mech->fw, CFX_FIRST_ACTION_FREE) > 0)
        DrawText("FIRST ACTION FREE", 420, 108, 10, (Color) { 120, 255, 180, 255 });
    // Active Firmware Corruption on the player
    const Firmware* fw = &battle.player.mech->fw;
    int offline = 0, reversed = 0;
    for (int k = 0; k < MAX_SOCKETS; k++) {
        if (fw->chips[k] < 0 || fw->corrupt[k] <= 0) continue;
        if (fw->corruptKind[k] == CORRUPT_REVERSED) reversed++; else offline++;
    }
    const char* corrupt = "";
    if (offline) corrupt = TextFormat("%s%d OFFLINE ", corrupt, offline);
    if (reversed) corrupt = TextFormat("%s%d REVERSED ", corrupt, reversed);
    if (battle.player.energyTax) corrupt = TextFormat("%sEN COST +%d ", corrupt, battle.player.energyTax);
    if (battle.player.randomTargeting) corrupt = TextFormat("%sRANDOM TARGETING", corrupt);
    if (corrupt[0]) DrawText(TextFormat("CORRUPTED: %s", corrupt), 340, 122, 10, (Color) { 255, 110, 90, 255 });
}

static void drawWeaponButton(int i) {
    Rectangle r = weaponButtonRect(i);
    int bx = (int)r.x, by = (int)r.y;
    const Weapon* w = mechWeapon(battle.player.mech, i);
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
    const char* info = w->baseDamage > 0
        ? TextFormat("HIT %d%%  ARM-%d INT-%d  HEAT+%d", (int)roundf(p.hitChance * 100), p.armorDamage, p.integrityDamage, p.heat)
        : TextFormat("HIT %d%%  SCRAMBLE %d  HEAT+%d", (int)roundf(p.hitChance * 100), p.scramble, p.heat);
    if (!usable && reason) info = reason;
    DrawText(info, bx + 24, by + 18, 10, usable ? (Color) { 150, 200, 220, 255 } : (Color) { 255, 120, 120, 255 });
    if (w->ammo > 0)
        DrawText(TextFormat("AMMO %d/%d", battle.player.mech->weapons[i].ammo, w->ammo), bx + 225, by + 18, 10,
            (Color) { 150, 200, 220, 255 });
    for (int e = 0; e < w->energyCost; e++) {
        int ex = bx + (int)r.width - 14 - e * 14, ey = by + 10;
        DrawCircle(ex, ey, 5, usable ? (Color) { 60, 200, 255, 255 } : (Color) { 70, 80, 100, 255 });
        DrawCircleLines(ex, ey, 5, (Color) { 180, 240, 255, 255 });
    }
}

// Every step of the attack formula for the selected weapon, before it is fired
static void drawPreviewPanel(const AttackPreview* p, const Weapon* w, int x, int y) {
    Color txt = { 210, 225, 240, 255 }, dim = { 130, 150, 175, 255 }, res = { 120, 255, 160, 255 };
    DrawText(TextFormat("PREVIEW  %s > %s", w->name, battle.enemyMech.name), x, y, 12, munitionColor(w->munition));
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

    BeginMode2D(layoutCamera());
    drawMechBattle(battle.enemyMech.model, (int)(enemyMechPos.x + sx), (int)(enemyMechPos.y + sy), 14, 1);
    drawMechBattle(battle.player.mech->model, (int)(playerMechPos.x + sx), (int)(playerMechPos.y + sy), 14, 0);
    drawEffects();
    drawParticles();
    drawDamageNums();

    drawEnemyHud(p);
    drawPlayerHud(p);
    drawReactor(p);

    // Log strip and bottom panel
    DrawRectangle(20, PANEL_Y - 22, SCREEN_W - 40, 20, (Color) { 10, 15, 30, 200 });
    DrawText(battle.log, 30, PANEL_Y - 18, 12, (Color) { 190, 220, 240, 255 });
    DrawRectangle(20, PANEL_Y, SCREEN_W - 40, 172, (Color) { 15, 20, 35, 240 });
    DrawRectangleLines(20, PANEL_Y, SCREEN_W - 40, 172, (Color) { 80, 220, 255, 200 });

    if (battle.phase == BP_PLAYER_TURN && battle.dialogue == DLG_NONE) {
        DrawText(">> SELECT WEAPON <<", 30, PANEL_Y + 6, 14, (Color) { 100, 240, 255, 255 });
        for (int i = 0; i < MAX_WEAPONS; i++) drawWeaponButton(i);
        drawButton(hackButtonRect(), "HACK [C]", 12, 0, battleCanHack());
        drawButton(endTurnButtonRect(), "END TURN [X]", 12, 0, 1);
        DrawLine(393, ROW_Y, 393, ROW_Y + 134, (Color) { 50, 90, 130, 200 });
        if (p) drawPreviewPanel(p, mechWeapon(battle.player.mech, weaponSel), 402, ROW_Y);
        else if (!battleBusy()) DrawText("No weapon on this mount.", 402, ROW_Y, 12, (Color) { 130, 150, 175, 255 });
        DrawText(battle.testRange ? "[Z] FIRE  [W/S] SELECT  [X] END TURN  [R] NEW DUMMY  [F1] DEBUG  [ESC] LEAVE"
                                  : "[Z] FIRE  [W/S] SELECT  [X] END TURN  [C] HACK  [F1] DEBUG",
            30, ROW_Y + 134, 10, (Color) { 100, 240, 255, 255 });
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
