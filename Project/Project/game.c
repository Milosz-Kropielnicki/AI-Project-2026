#include "game.h"
#include "world.h"
#include "firmware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int credits = START_CREDITS;
int weaponOwned[NUM_WEAPONS];
int moduleOwned[NUM_REFIT_MODULES];
int jobState[NUM_JOBS];
int jobProgress[NUM_JOBS];

int factionHackable(int faction) { return factions[faction].kind == FKIND_ROGUE_AI; }

// ============ PRICES ============
int weaponPrice(int weapon) {
    const Weapon* w = &weaponTable[weapon];
    return 50 + w->baseDamage * 5 + w->scramble * 2 + w->armorPen;
}

int modulePrice(int module) {
    float size = 0;
    for (int s = 0; s < NUM_STATS; s++) {
        float d = refitModules[module].delta.v[s];
        size += (s == STAT_POWER || s == STAT_ENERGY) ? (d < 0 ? -d : d) * 40 : (d < 0 ? -d : d);
    }
    return 150 + (int)size * 2;
}

int chipPrice(int chip) { return 80 * chipDefs[chip].cost * (chipDefs[chip].rarity + 1); }
int chipForSale(int chip) { return chipDefs[chip].rarity <= RARITY_EXPERIMENTAL; }

// ============ REPAIR ============
static int missingIntegrity(const Mech* m) { return m->stats.maxIntegrity - m->stats.integrity; }

int teamRepairCost(void) {
    int cost = 0;
    for (int i = 0; i < teamSize; i++)
        cost += (missingIntegrity(&team[i]) + REPAIR_INT_PER_CREDIT - 1) / REPAIR_INT_PER_CREDIT;
    return cost;
}

// Armor is replated for free after every battle; Integrity costs credits.
// With too few credits, the active mech is repaired first.
int teamRepair(void) {
    int spent = 0;
    for (int k = 0; k < teamSize; k++) {
        Mech* m = &team[(activeTeamSlot + k) % teamSize];
        int need = (missingIntegrity(m) + REPAIR_INT_PER_CREDIT - 1) / REPAIR_INT_PER_CREDIT;
        int pay = need < credits ? need : credits;
        credits -= pay;
        spent += pay;
        if (pay == need) mechRepair(m);
        else {
            m->stats.integrity += pay * REPAIR_INT_PER_CREDIT;
            mechReplate(m);
        }
    }
    return spent;
}

// ============ PARTS ============
int moduleIsStandard(int module) {
    for (int s = 0; s < NUM_REFIT_SLOTS; s++) if (refitStandard[s] == module) return 1;
    return 0;
}

int weaponAvailable(int weapon) {
    int used = 0;
    for (int t = 0; t < teamSize; t++)
        for (int i = 0; i < MAX_WEAPONS; i++) if (team[t].weapons[i].weapon == weapon) used++;
    return weaponOwned[weapon] - used;
}

int moduleAvailable(int module) {
    if (moduleIsStandard(module)) return 99;
    int used = 0;
    for (int t = 0; t < teamSize; t++)
        for (int s = 0; s < NUM_REFIT_SLOTS; s++) if (team[t].refit[s] == module) used++;
    return moduleOwned[module] - used;
}

void partsAddFromMech(const Mech* m) {
    for (int i = 0; i < MAX_WEAPONS; i++) if (m->weapons[i].weapon >= 0) weaponOwned[m->weapons[i].weapon]++;
    for (int s = 0; s < NUM_REFIT_SLOTS; s++) if (!moduleIsStandard(m->refit[s])) moduleOwned[m->refit[s]]++;
    for (int s = 0; s < MAX_SOCKETS; s++) if (m->fw.chips[s] >= 0) chipOwned[m->fw.chips[s]]++;
}

// ============ PURCHASES ============
static const char* pay(int price) {
    if (credits < price) return TextFormat("Not enough credits (%d needed)", price);
    credits -= price;
    return NULL;
}

const char* buyMech(int entry) {
    const CatalogEntry* e = &catalog[entry];
    if (teamSize >= MAX_TEAM) return "Team is full";
    const char* err = pay(e->price);
    if (err) return err;
    Mech m = mechCreateStock(e->chassis, e->level);
    partsAddFromMech(&m);
    rosterAdd(&m);
    return NULL;
}

const char* buyWeapon(int weapon) {
    const char* err = pay(weaponPrice(weapon));
    if (!err) weaponOwned[weapon]++;
    return err;
}

const char* buyModule(int module) {
    if (moduleIsStandard(module)) return "Standard parts are free";
    const char* err = pay(modulePrice(module));
    if (!err) moduleOwned[module]++;
    return err;
}

const char* buyChip(int chip) {
    if (!chipForSale(chip)) return "Not for sale";
    const char* err = pay(chipPrice(chip));
    if (!err) chipOwned[chip]++;
    return err;
}

// ============ JOBS ============
static int jobTrainer(int job) { return jobDefs[job].trainer ? worldFindTrainer(jobDefs[job].trainer) : -1; }

int jobsActive(void) {
    int n = 0;
    for (int j = 0; j < NUM_JOBS; j++) if (jobState[j] == JS_ACTIVE || jobState[j] == JS_READY) n++;
    return n;
}

static int jobGoal(int job) { return jobDefs[job].type == JOB_CULL ? jobDefs[job].count : 1; }

// Advance an active job; returns a note when it becomes ready to claim
static const char* jobAdvance(int job) {
    if (jobState[job] != JS_ACTIVE) return NULL;
    if (++jobProgress[job] < jobGoal(job)) return NULL;
    jobState[job] = JS_READY;
    return TextFormat(" JOB READY: %s.", jobDefs[job].title);
}

const char* jobAccept(int job) {
    if (jobState[job] != JS_OPEN) return "Already taken";
    if (jobsActive() >= MAX_ACTIVE_JOBS) return TextFormat("Max %d active jobs", MAX_ACTIVE_JOBS);
    jobState[job] = JS_ACTIVE;
    jobProgress[job] = 0;
    int t = jobTrainer(job);   // bounty on an encounter that is already down
    if (jobDefs[job].type == JOB_BOUNTY && t >= 0 && trainers[t].defeated) jobAdvance(job);
    return NULL;
}

static void grantReward(RewardKind kind, int item) {
    switch (kind) {
    case REWARD_WEAPON: weaponOwned[item]++; break;
    case REWARD_MODULE: moduleOwned[item]++; break;
    case REWARD_CHIP:   chipOwned[item]++; break;
    case REWARD_MECH: {
        Mech m = mechCreateStock(item, gameWildLevel(ZONE_BETA));
        partsAddFromMech(&m);
        rosterAdd(&m);
        break;
    }
    default: break;
    }
}

const char* jobClaim(int job) {
    const JobDef* d = &jobDefs[job];
    if (jobState[job] != JS_READY) return "Job not complete";
    if (d->reward == REWARD_MECH && teamSize >= MAX_TEAM) return "Team is full - make room for the unit";
    credits += d->credits;
    grantReward(d->reward, d->rewardItem);
    jobState[job] = JS_DONE;
    return NULL;
}

const char* jobObjective(int job) {
    const JobDef* d = &jobDefs[job];
    const char* zone = zones[d->zone].name;
    switch (d->type) {
    case JOB_BOUNTY:  return TextFormat("Defeat %s in %s", d->trainer, zone);
    case JOB_CULL:    return TextFormat("Scrap rogue machines in %s  %d/%d", zone, jobProgress[job], d->count);
    default:          return TextFormat("Reprogram a rogue %s in %s", d->archetype >= 0 ? archetypes[d->archetype].name : "machine", zone);
    }
}

const char* rewardName(RewardKind kind, int item) {
    switch (kind) {
    case REWARD_WEAPON: return TextFormat("weapon: %s", weaponTable[item].name);
    case REWARD_MODULE: return TextFormat("refit: %s", refitModules[item].name);
    case REWARD_CHIP:   return TextFormat("chip: %s", chipDefs[item].name);
    case REWARD_MECH:   return TextFormat("unit: %s %s", mechModels[item].designation, mechModels[item].name);
    default:            return "";
    }
}

// ============ BATTLE EVENTS ============
int gameWildLevel(int zone) {
    static const int lo[NUM_ZONES] = { 0, 2, 5 }, hi[NUM_ZONES] = { 1, 4, 7 };
    return lo[zone] + rand() % (hi[zone] - lo[zone] + 1);
}

static void append(char* note, int size, const char* s) {
    if (s) snprintf(note + strlen(note), size - strlen(note), "%s", s);
}

void gameOnWildScrapped(const Mech* enemy, char* note, int size) {
    int pay = 15 + 8 * enemy->fw.revision;
    credits += pay;
    snprintf(note, size, " +%d CR.", pay);
    // Salvage: a 30% chance to pull one of its weapons out intact
    int mounts[MAX_WEAPONS], n = 0;
    for (int i = 0; i < MAX_WEAPONS; i++) if (enemy->weapons[i].weapon >= 0) mounts[n++] = i;
    if (n > 0 && rand() % 100 < 30) {
        int w = enemy->weapons[mounts[rand() % n]].weapon;
        weaponOwned[w]++;
        append(note, size, TextFormat(" Salvaged %s.", weaponTable[w].name));
    }
    int zone = worldCurrentZone();
    for (int j = 0; j < NUM_JOBS; j++)
        if (jobDefs[j].type == JOB_CULL && jobDefs[j].zone == zone) append(note, size, jobAdvance(j));
}

void gameOnHacked(const Mech* caught, int archetype, char* note, int size) {
    partsAddFromMech(caught);
    note[0] = 0;
    int zone = worldCurrentZone();
    for (int j = 0; j < NUM_JOBS; j++) {
        const JobDef* d = &jobDefs[j];
        if (d->type == JOB_RECOVER && d->zone == zone && (d->archetype < 0 || d->archetype == archetype))
            append(note, size, jobAdvance(j));
    }
}

void gameOnEncounterDefeated(int trainer, char* note, int size) {
    int prize = 100 + 150 * trainers[trainer].tier;
    credits += prize;
    snprintf(note, size, " +%d CR prize.", prize);
    for (int j = 0; j < NUM_JOBS; j++)
        if (jobDefs[j].type == JOB_BOUNTY && jobTrainer(j) == trainer) append(note, size, jobAdvance(j));
}

void gameOnPlayerDisabled(char* note, int size) {
    int fee = credits * DEFEAT_FEE_PERCENT / 100;
    credits -= fee;
    snprintf(note, size, fee > 0 ? " Recovery crew fee: -%d CR." : "", fee);
}

// ============ SESSION ============
static void partsInit(void) {
    memset(weaponOwned, 0, sizeof(weaponOwned));
    memset(moduleOwned, 0, sizeof(moduleOwned));
    for (int i = 0; i < teamSize; i++) partsAddFromMech(&team[i]);
    // chips were counted by chipCollectionInit; undo the double count from partsAddFromMech
    for (int i = 0; i < teamSize; i++)
        for (int s = 0; s < MAX_SOCKETS; s++) if (team[i].fw.chips[s] >= 0) chipOwned[team[i].fw.chips[s]]--;
    weaponOwned[W_SHOTGUN]++;
    weaponOwned[W_PULSE_LASER]++;
}

void gameNew(void) {
    worldInit();                        // map generation reseeds with fixed seeds
    srand((unsigned)time(NULL));
    chipCollectionInit();
    rosterInit();
    partsInit();
    credits = START_CREDITS;
    memset(jobState, 0, sizeof(jobState));
    memset(jobProgress, 0, sizeof(jobProgress));
}

// ============ SAVE / LOAD ============
// A flat binary snapshot. Mech and Firmware are plain data, so the team is
// written as-is; the header rejects saves from builds with a different layout.
#define SAVE_FILE "savegame.dat"
#define SAVE_VERSION 1

typedef struct {
    char magic[4];
    int version;
    int layout[4];      // sizeof checks: Mech, chips, weapons, modules
    Mech team[MAX_TEAM];
    int teamSize, activeSlot;
    int chipOwned[NUM_CHIPS];
    int weaponOwned[NUM_WEAPONS];
    int moduleOwned[NUM_REFIT_MODULES];
    int credits;
    int jobState[NUM_JOBS], jobProgress[NUM_JOBS];
    int trainerDefeated[NUM_TRAINERS];
    int zone, x, y;
} SaveData;

static void saveLayout(int* layout) {
    layout[0] = (int)sizeof(Mech);
    layout[1] = NUM_CHIPS;
    layout[2] = NUM_WEAPONS;
    layout[3] = NUM_REFIT_MODULES * 1000 + NUM_JOBS * 10 + NUM_TRAINERS;
}

int gameSave(void) {
    static SaveData d;
    memset(&d, 0, sizeof(d));
    memcpy(d.magic, "MBSV", 4);
    d.version = SAVE_VERSION;
    saveLayout(d.layout);
    memcpy(d.team, team, sizeof(d.team));
    d.teamSize = teamSize;
    d.activeSlot = activeTeamSlot;
    memcpy(d.chipOwned, chipOwned, sizeof(d.chipOwned));
    memcpy(d.weaponOwned, weaponOwned, sizeof(d.weaponOwned));
    memcpy(d.moduleOwned, moduleOwned, sizeof(d.moduleOwned));
    d.credits = credits;
    memcpy(d.jobState, jobState, sizeof(d.jobState));
    memcpy(d.jobProgress, jobProgress, sizeof(d.jobProgress));
    for (int i = 0; i < NUM_TRAINERS; i++) d.trainerDefeated[i] = trainers[i].defeated;
    worldGetPlayer(&d.zone, &d.x, &d.y);

    FILE* f = fopen(SAVE_FILE, "wb");
    if (!f) return 0;
    int ok = fwrite(&d, sizeof(d), 1, f) == 1;
    fclose(f);
    return ok;
}

static int readSave(SaveData* d) {
    FILE* f = fopen(SAVE_FILE, "rb");
    if (!f) return 0;
    int ok = fread(d, sizeof(*d), 1, f) == 1;
    fclose(f);
    int layout[4];
    saveLayout(layout);
    return ok && memcmp(d->magic, "MBSV", 4) == 0 && d->version == SAVE_VERSION
        && memcmp(d->layout, layout, sizeof(layout)) == 0;
}

int gameSaveExists(void) {
    static SaveData d;
    return readSave(&d);
}

int gameLoad(void) {
    static SaveData d;
    if (!readSave(&d)) return 0;
    worldInit();
    srand((unsigned)time(NULL));
    memcpy(team, d.team, sizeof(team));
    teamSize = d.teamSize;
    activeTeamSlot = d.activeSlot;
    memcpy(chipOwned, d.chipOwned, sizeof(chipOwned));
    memcpy(weaponOwned, d.weaponOwned, sizeof(weaponOwned));
    memcpy(moduleOwned, d.moduleOwned, sizeof(moduleOwned));
    credits = d.credits;
    memcpy(jobState, d.jobState, sizeof(jobState));
    memcpy(jobProgress, d.jobProgress, sizeof(jobProgress));
    for (int i = 0; i < NUM_TRAINERS; i++) trainers[i].defeated = d.trainerDefeated[i];
    worldSetPlayer(d.zone, d.x, d.y);
    return 1;
}
