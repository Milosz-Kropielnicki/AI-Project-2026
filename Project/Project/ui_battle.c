#include "ui.h"
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

static Rectangle weaponButtonRect(int i) {
    int col = i % 2, row = i / 2;
    return (Rectangle) { 35.0f + col * 380, SCREEN_H - 135.0f + row * 58, 350, 48 };
}
static Rectangle hackButtonRect(void) { return (Rectangle) { 460, SCREEN_H - 157, 130, 20 }; }
static Rectangle endTurnButtonRect(void) { return (Rectangle) { 600, SCREEN_H - 157, 170, 20 }; }

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

    if (RIGHT_PRESSED) weaponSel = (weaponSel + 1) % MAX_WEAPONS;
    if (LEFT_PRESSED)  weaponSel = (weaponSel + MAX_WEAPONS - 1) % MAX_WEAPONS;
    if (DOWN_PRESSED)  weaponSel = (weaponSel + 2) % MAX_WEAPONS;
    if (UP_PRESSED)    weaponSel = (weaponSel + MAX_WEAPONS - 2) % MAX_WEAPONS;

    // Mouse: hovering selects a weapon, clicking fires it
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
            const WeaponDef* w = mechWeapon(battle.player.mech, weaponSel);
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

static void drawEnergyPip(int cx, int cy, int r, int active, int i) {
    Color fill = active ? (Color) { 60, 200, 255, 255 } : (Color) { 40, 45, 60, 255 };
    Color edge = active ? (Color) { 180, 240, 255, 255 } : (Color) { 70, 80, 100, 255 };
    if (active) {
        float p = 0.5f + 0.5f * sinf(glowTimer * 5 + i);
        DrawCircle(cx, cy, r + p * 3, (Color) { 60, 180, 255, 40 });
    }
    DrawCircle(cx, cy, (float)r, fill);
    DrawCircleLines(cx, cy, (float)r, edge);
    DrawCircle(cx - r / 3, cy - r / 3, r / 4.0f, (Color) { 255, 255, 255, (unsigned char)(active ? 220 : 40) });
}

static void drawEnemyHud(void) {
    const Mech* m = &battle.enemyMech;
    const MechModel* model = mechModel(m);
    int maxI = (int)battle.enemy.stats.v[STAT_INTEGRITY], maxA = (int)battle.enemy.stats.v[STAT_ARMOR];
    DrawRectangle(20, 20, 300, 96, (Color) { 20, 25, 40, 230 });
    if (battle.trainer >= 0) {
        Trainer* t = &trainers[battle.trainer];
        DrawRectangleLines(20, 20, 300, 96, (Color) { 255, 200, 60, 220 });
        DrawText(TextFormat("%s  %s", t->team, t->name), 28, 24, 12, (Color) { 255, 220, 100, 255 });
        DrawText(TextFormat("MECH %d/%d", t->numDefeated + 1, t->numMechs), 250, 24, 12, (Color) { 255, 200, 100, 255 });
    }
    else {
        DrawRectangleLines(20, 20, 300, 96, (Color) { 255, 80, 80, 220 });
        DrawText("HOSTILE", 28, 24, 12, (Color) { 255, 100, 100, 255 });
    }
    DrawText(m->name, 30, 38, 22, (Color) { 255, 210, 210, 255 });
    DrawText(TextFormat("FW %s", firmwareLabel(m->fw.revision)), 250, 40, 18, WHITE);
    DrawText(TextFormat("%s %s - %s", model->designation, model->name, roleNames[model->role]),
        30, 60, 11, (Color) { 200, 200, 240, 255 });
    drawIntegrityBar(30, 76, 280, 10, m->integrity, maxI);
    drawArmorBar(30, 90, 280, 6, m->armor, maxA);
    DrawText(TextFormat("INT %d/%d", m->integrity, maxI), 30, 100, 11, WHITE);
    DrawText(TextFormat("ARM %d/%d", m->armor, maxA), 130, 100, 11, (Color) { 150, 190, 240, 255 });
    if (battleCanHack())
        DrawText(TextFormat("HACK %d%%", (int)roundf(hackChance(m, &battle.enemy.stats) * 100)),
            240, 100, 11, (Color) { 120, 255, 220, 255 });
}

static void drawPlayerHud(void) {
    const Combatant* c = &battle.player;
    const Mech* m = c->mech;
    const MechModel* model = mechModel(m);
    int maxI = (int)c->stats.v[STAT_INTEGRITY], maxA = (int)c->stats.v[STAT_ARMOR];
    DrawRectangle(400, 200, 300, 130, (Color) { 20, 30, 50, 230 });
    DrawRectangleLines(400, 200, 300, 130, model->accent);
    DrawText("ALLIED", 408, 204, 12, (Color) { 100, 240, 255, 255 });
    DrawText(m->name, 410, 218, 22, model->accent);
    DrawText(TextFormat("FW %s", firmwareLabel(m->fw.revision)), 630, 220, 18, WHITE);
    drawIntegrityBar(410, 246, 280, 10, m->integrity, maxI);
    DrawText(TextFormat("INT %d/%d", m->integrity, maxI), 410, 258, 11, WHITE);
    drawArmorBar(410, 272, 280, 6, m->armor, maxA);
    DrawText(TextFormat("ARM %d/%d", m->armor, maxA), 510, 258, 11, (Color) { 150, 190, 240, 255 });
    drawHeatBar(410, 284, 280, 6, c->heat, (int)c->stats.v[STAT_HEAT]);
    DrawText(TextFormat("HEAT %d/%d  (-%d/turn)", c->heat, (int)c->stats.v[STAT_HEAT], (int)c->stats.v[STAT_COOLING]),
        410, 293, 11, (Color) { 255, 170, 100, 255 });
    int need = firmwareDataToNext(m->fw.revision);
    drawDataBar(410, 308, 280, 5, m->fw.data, need);
    DrawText(TextFormat("DATA %d/%d", m->fw.data, need), 410, 316, 10, (Color) { 200, 170, 255, 255 });
    if (c->accPenalty > 0 || c->disabledWeapon >= 0)
        DrawText(TextFormat("SCRAMBLED%s%s", c->accPenalty > 0 ? TextFormat(" ACC-%d", c->accPenalty) : "",
            c->disabledWeapon >= 0 ? " WPN OFFLINE" : ""), 560, 316, 10, (Color) { 200, 150, 255, 255 });
}

static void drawReactor(void) {
    const Combatant* c = &battle.player;
    int pips = (int)c->stats.v[STAT_ENERGY];
    if (c->energy > pips) pips = c->energy;
    DrawRectangle(330, 20, 200, 80, (Color) { 15, 25, 45, 230 });
    DrawRectangleLines(330, 20, 200, 80, (Color) { 100, 200, 255, 220 });
    DrawText("REACTOR", 400, 25, 14, (Color) { 150, 220, 255, 255 });
    int spacing = pips > 4 ? 26 : 40;
    int r = pips > 4 ? 11 : 14;
    int x0 = 430 - (pips - 1) * spacing / 2;
    for (int i = 0; i < pips; i++) drawEnergyPip(x0 + i * spacing, 64, r, i < c->energy, i);
    DrawText(TextFormat("ROUND %d", battle.round), 340, 106, 14, (Color) { 150, 220, 255, 255 });
    if (c->actionsThisTurn == 0 && firmwareChipTotal(&c->mech->fw, CFX_FIRST_ACTION_FREE) > 0)
        DrawText("FIRST ACTION FREE", 420, 108, 10, (Color) { 120, 255, 180, 255 });
}

static void drawWeaponButton(int i) {
    Rectangle mr = weaponButtonRect(i);
    int bx = (int)mr.x + 5, by = (int)mr.y + 5;
    const WeaponDef* w = mechWeapon(battle.player.mech, i);
    int selected = (i == weaponSel);
    if (!w) {
        DrawRectangleLines(bx - 5, by - 5, 350, 48, selected ? (Color) { 120, 120, 120, 255 } : (Color) { 50, 70, 100, 200 });
        DrawText("-- EMPTY MOUNT --", bx + 20, by + 8, 16, (Color) { 90, 100, 120, 255 });
        return;
    }
    const char* reason = NULL;
    int usable = battleCanFire(i, &reason) || (reason && strcmp(reason, "STANDBY") == 0);
    Color col = munitionColor(w->munition);
    Color border = selected ? (usable ? col : (Color) { 120, 120, 120, 255 }) : (Color) { 60, 120, 180, 200 };
    Color textCol = usable ? (Color) { 220, 240, 255, 255 } : (Color) { 110, 120, 140, 255 };
    if (selected && usable) DrawRectangle(bx - 5, by - 5, 350, 48, (Color) { col.r, col.g, col.b, 60 });
    DrawRectangleLines(bx - 5, by - 5, 350, 48, border);
    DrawCircle(bx + 8, by + 12, 6, usable ? col : (Color) { 90, 90, 100, 255 });
    DrawCircleLines(bx + 8, by + 12, 6, WHITE);
    DrawText(w->name, bx + 20, by + 2, 18, textCol);

    AttackPreview p;
    battlePreviewPlayer(i, &p);
    const char* info = w->baseDamage > 0
        ? TextFormat("HIT %d%%  ARM-%d INT-%d  HEAT +%d", (int)roundf(p.hitChance * 100), p.armorDamage, p.integrityDamage, p.heat)
        : TextFormat("HIT %d%%  SCR %d  HEAT +%d", (int)roundf(p.hitChance * 100), p.scramble, p.heat);
    DrawText(info, bx + 20, by + 23, 11, (Color) { 150, 200, 220, 255 });
    if (w->ammo > 0)
        DrawText(TextFormat("AMMO %d/%d", battle.player.mech->weapons[i].ammo, w->ammo), bx + 200, by + 5, 11,
            (Color) { 150, 200, 220, 255 });
    if (!usable && reason) DrawText(reason, bx + 200, by + 23, 11, (Color) { 255, 120, 120, 255 });

    for (int e = 0; e < w->energyCost; e++) {
        int ex = bx + 290 + e * 16, ey = by + 12;
        DrawCircle(ex, ey, 6, usable ? (Color) { 60, 200, 255, 255 } : (Color) { 70, 80, 100, 255 });
        DrawCircleLines(ex, ey, 6, (Color) { 180, 240, 255, 255 });
    }
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

    BeginMode2D(layoutCamera());
    drawMechBattle(battle.enemyMech.model, (int)(enemyMechPos.x + sx), (int)(enemyMechPos.y + sy), 14, 1);
    drawMechBattle(battle.player.mech->model, (int)(playerMechPos.x + sx), (int)(playerMechPos.y + sy), 14, 0);
    drawEffects();
    drawParticles();
    drawDamageNums();

    drawEnemyHud();
    drawPlayerHud();
    drawReactor();

    // Bottom panel
    DrawRectangle(20, SCREEN_H - 160, SCREEN_W - 40, 140, (Color) { 15, 20, 35, 240 });
    DrawRectangleLines(20, SCREEN_H - 160, SCREEN_W - 40, 140, (Color) { 80, 220, 255, 200 });

    if (battle.phase == BP_PLAYER_TURN && battle.dialogue == DLG_NONE) {
        DrawText(">> SELECT WEAPON <<", 30, SCREEN_H - 155, 14, (Color) { 100, 240, 255, 255 });
        for (int i = 0; i < MAX_WEAPONS; i++) drawWeaponButton(i);
        drawButton(hackButtonRect(), "HACK [C]", 12, 0, battleCanHack());
        drawButton(endTurnButtonRect(), "END TURN [X]", 12, 0, 1);
        // Log of the last action sits just above the panel so long lines don't hit the buttons
        DrawRectangle(20, SCREEN_H - 184, SCREEN_W - 40, 20, (Color) { 10, 15, 30, 200 });
        DrawText(battle.log, 30, SCREEN_H - 180, 12, (Color) { 180, 210, 230, 255 });
        DrawText("[Z/ENTER/CLICK] FIRE   [WASD/ARROWS] SELECT   [F1] DEBUG",
            30, SCREEN_H - 18, 14, (Color) { 100, 240, 255, 255 });
    }
    else {
        DrawText(">> SYS LOG <<", 30, SCREEN_H - 155, 14, (Color) { 100, 240, 255, 255 });
        DrawText(battle.log, 35, SCREEN_H - 130, 20, (Color) { 220, 240, 255, 255 });

        if (battle.phase == BP_VICTORY && battle.dialogue == DLG_NONE && !battleBusy()) {
            if (battle.hacked)
                DrawText(">> SYSTEM REPROGRAMMED! [Z/CLICK] <<", 35, SCREEN_H - 75, 20, (Color) { 120, 255, 220, 255 });
            else if (battle.trainer >= 0)
                DrawText(">> VICTORY! [Z/CLICK] TO CONTINUE <<", 35, SCREEN_H - 75, 20, (Color) { 255, 220, 100, 255 });
            else
                DrawText(">> TARGET SCRAPPED! [Z/CLICK] <<", 35, SCREEN_H - 75, 20, (Color) { 120, 255, 180, 255 });
            DrawText(TextFormat("+%d REVISION DATA", battle.dataEarned), 480, SCREEN_H - 75, 20, (Color) { 200, 170, 255, 255 });
        }
        if (battle.phase == BP_DEFEAT && battle.dialogue == DLG_NONE)
            DrawText(">> MECH DISABLED! [Z/CLICK] TO CONTINUE <<", 35, SCREEN_H - 75, 20, (Color) { 255, 100, 100, 255 });

        if (battle.dialogue != DLG_NONE) {
            DrawRectangle(20, SCREEN_H - 260, SCREEN_W - 40, 100, (Color) { 10, 15, 30, 245 });
            DrawRectangleLines(20, SCREEN_H - 260, SCREEN_W - 40, 100, (Color) { 255, 220, 100, 240 });
            if (battle.trainer >= 0) {
                Trainer* t = &trainers[battle.trainer];
                DrawText(TextFormat("%s  %s", t->team, t->name), 35, SCREEN_H - 252, 14, (Color) { 255, 220, 100, 255 });
            }
            DrawText(battle.dialogueText, 40, SCREEN_H - 225, 22, (Color) { 255, 240, 220, 255 });
            DrawText("[Z/CLICK] to continue", SCREEN_W - 220, SCREEN_H - 180, 16, (Color) { 150, 200, 255, 255 });
        }
    }
    EndMode2D();
}

// ============ FIRMWARE REVISION SCREEN ============
static int revFrom = 0, revTo = 0, optSel = 0;
static float revTimer = 0;

static Rectangle optionRect(int i) {
    int col = i % 3, row = i / 3;
    return (Rectangle) { 130.0f + col * 185, 430.0f + row * 50, 175, 40 };
}

void uiRevisionOpen(void) {
    revFrom = battle.oldRevision;
    revTo = rosterActive()->fw.revision;
    optSel = 0;
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
    else if (revTimer > 0.4f)
        drawTextCentered("[Z/CLICK] to continue", SCREEN_H - 40, 20, (Color) { 100, 240, 255, 255 });
    EndMode2D();
}
