#include "mech.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============ STATS ============
static const char* statNames[NUM_STATS] = {
    "INTEGRITY", "POWER", "ARMOR", "MOBILITY", "ENERGY", "HEAT CAP", "COOLING", "ACCURACY", "STABILITY"
};
static const char* statShorts[NUM_STATS] = { "INT", "PWR", "ARM", "MOB", "EN", "HEAT", "COOL", "ACC", "STB" };
static const float statMin[NUM_STATS] = { 1, 0.50f, 0, 0, 1, 0, 0, 0, 0 };
static const float statMax[NUM_STATS] = { 200, 2.00f, 150, 100, 5, 200, 200, 100, 100 };

const char* statName(StatId s) { return statNames[s]; }
const char* statShort(StatId s) { return statShorts[s]; }

const char* statFormat(StatId s, float value) {
    switch (s) {
    case STAT_POWER: return TextFormat("%.2fx", value);
    case STAT_MOBILITY: case STAT_ACCURACY: case STAT_STABILITY: return TextFormat("%d%%", (int)roundf(value));
    default: return TextFormat("%d", (int)roundf(value));
    }
}

float statClamp(StatId s, float value) {
    if (value < statMin[s]) value = statMin[s];
    if (value > statMax[s]) value = statMax[s];
    return s == STAT_POWER ? value : roundf(value);   // everything but Power is a whole number
}

void statsAdd(Stats* dst, const Stats* src) {
    for (int i = 0; i < NUM_STATS; i++) dst->v[i] += src->v[i];
}

// ============ REFIT ============
// Rating 5 keeps the full upside of a module; lower ratings shrink the upside
// while drawbacks stay at full strength, so a 1-star module is a net loss.
float refitRatingFactor(int rating) {
    static const float factor[6] = { 0.0f, 0.10f, 0.40f, 0.65f, 0.85f, 1.0f };
    if (rating < 1) rating = 1;
    if (rating > 5) rating = 5;
    return factor[rating];
}

int refitRating(const Mech* m, int module) {
    return refitModules[module].rating[mechClass(m)];
}

// ============ MECH ============
const char* statLayerNames[NUM_STAT_LAYERS] = { "ROLE", "MODEL", "REFIT", "FIRMWARE", "CHIPS" };

const MechModel* mechModel(const Mech* m) { return &mechModels[m->model]; }
MechClass mechClass(const Mech* m) { return (MechClass)(mechModels[m->model].role / ROLES_PER_CLASS); }

void mechStatBreakdown(const Mech* m, StatBreakdown* out) {
    memset(out, 0, sizeof(*out));
    const MechModel* model = mechModel(m);
    out->layer[LAYER_ROLE] = roleBaseline[model->role];
    out->layer[LAYER_MODEL] = model->delta;
    for (int slot = 0; slot < NUM_REFIT_SLOTS; slot++) {
        const RefitModule* mod = &refitModules[m->refit[slot]];
        float f = refitRatingFactor(refitRating(m, m->refit[slot]));
        for (int s = 0; s < NUM_STATS; s++) {
            float d = mod->delta.v[s];
            out->layer[LAYER_REFIT].v[s] += d > 0 ? d * f : d;
        }
    }
    for (int s = 0; s < NUM_STATS; s++) {
        out->layer[LAYER_FIRMWARE].v[s] = firmwareStatBonus(&m->fw, (StatId)s);
        out->layer[LAYER_CHIPS].v[s] = firmwareChipStat(&m->fw, (StatId)s);
    }
    for (int l = 0; l < NUM_STAT_LAYERS; l++) statsAdd(&out->sum, &out->layer[l]);
    for (int s = 0; s < NUM_STATS; s++) out->final.v[s] = statClamp((StatId)s, out->sum.v[s]);
}

// Rebuild maximums and fixed attributes from the stat layers, keeping the
// current pools (clamped to their new maximums).
void mechRefreshStats(Mech* m) {
    StatBreakdown b;
    mechStatBreakdown(m, &b);
    const float* v = b.final.v;
    MechStats* s = &m->stats;
    s->maxIntegrity = (int)v[STAT_INTEGRITY];
    s->maxArmor = (int)v[STAT_ARMOR];
    s->power = v[STAT_POWER];
    s->mobility = (int)v[STAT_MOBILITY];
    s->maxEnergy = (int)v[STAT_ENERGY];
    s->maxHeat = (int)v[STAT_HEAT];
    s->cooling = (int)v[STAT_COOLING];
    s->accuracy = (int)v[STAT_ACCURACY];
    s->stability = (int)v[STAT_STABILITY];
    if (s->integrity > s->maxIntegrity) s->integrity = s->maxIntegrity;
    if (s->armor > s->maxArmor) s->armor = s->maxArmor;
    if (s->heat > s->maxHeat) s->heat = s->maxHeat;
}

void mechRepair(Mech* m) {
    mechRefreshStats(m);
    m->stats.integrity = m->stats.maxIntegrity;
    m->stats.armor = m->stats.maxArmor;
    m->stats.energy = m->stats.maxEnergy;
    m->stats.heat = 0;
}

void mechReplate(Mech* m) {
    mechRefreshStats(m);
    m->stats.armor = m->stats.maxArmor;
}

void mechReloadWeapons(Mech* m) {
    for (int i = 0; i < MAX_WEAPONS; i++)
        m->weapons[i].ammo = m->weapons[i].weapon >= 0 ? weaponTable[m->weapons[i].weapon].ammo : 0;
}

void mechSetWeapon(Mech* m, int mount, int weapon) {
    if (mount < 0 || mount >= MAX_WEAPONS) return;
    m->weapons[mount].weapon = weapon;
    m->weapons[mount].ammo = weapon >= 0 ? weaponTable[weapon].ammo : 0;
}

void mechSetRefit(Mech* m, RefitSlot slot, int module) {
    if (module < 0 || module >= NUM_REFIT_MODULES || refitModules[module].slot != slot) return;
    m->refit[slot] = module;
    mechRefreshStats(m);
}

const Weapon* mechWeapon(const Mech* m, int mount) {
    if (mount < 0 || mount >= MAX_WEAPONS || m->weapons[mount].weapon < 0) return NULL;
    return &weaponTable[m->weapons[mount].weapon];
}

int mechNumWeapons(const Mech* m) {
    int n = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) if (m->weapons[i].weapon >= 0) n++;
    return n;
}

Mech mechCreate(int model, int revision) {
    Mech m;
    memset(&m, 0, sizeof(m));
    m.model = model;
    snprintf(m.name, sizeof(m.name), "%s-%d", mechModels[model].name, 10 + rand() % 90);
    for (int i = 0; i < MAX_WEAPONS; i++) mechSetWeapon(&m, i, mechModels[model].weapons[i]);
    for (int s = 0; s < NUM_REFIT_SLOTS; s++) m.refit[s] = refitStandard[s];
    firmwareInit(&m.fw, revision);
    firmwareAutoSpend(&m.fw);
    mechRepair(&m);
    return m;
}

// ============ ROSTER ============
Mech team[MAX_TEAM];
int teamSize = 0;
int activeTeamSlot = 0;

void rosterInit(void) {
    team[0] = mechCreate(MODEL_NOVA, 0);
    strncpy(team[0].name, "NOVA-7", sizeof(team[0].name) - 1);
    firmwareInstall(&team[0].fw, 0, CHIP_PREDICTIVE_TARGETING);
    mechRepair(&team[0]);   // also picks up the chip's stat bonus
    teamSize = 1;
    activeTeamSlot = 0;
}

int rosterAdd(const Mech* m) {
    if (teamSize >= MAX_TEAM) return 0;
    team[teamSize++] = *m;
    return 1;
}

Mech* rosterActive(void) { return &team[activeTeamSlot]; }

int chipAvailable(int chip) {
    int used = 0;
    for (int t = 0; t < teamSize; t++)
        for (int s = 0; s < MAX_SOCKETS; s++)
            if (team[t].fw.chips[s] == chip) used++;
    return chipOwned[chip] - used;
}
