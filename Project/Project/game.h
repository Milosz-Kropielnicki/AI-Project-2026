#ifndef GAME_H
#define GAME_H

#include "raylib.h"
#include "mech.h"

// The campaign layer around the battle engine (design doc 1-2): the player is a
// mercenary combat engineer taking jobs against Rogue AI and Criminal
// Organizations. Loop: Explore -> Job -> Battle -> Hack/Scrap -> Upgrade.

// ============ FACTIONS ============
typedef enum { FKIND_CRIMINAL, FKIND_ROGUE_AI } FactionKind;

typedef struct {
    const char* name;
    FactionKind kind;
    Color color;
    const char* desc;
} Faction;

enum { FAC_IRON_LEGION, FAC_CHROME_SYNDICATE, FAC_BLACK_BOX, NUM_FACTIONS };
#define FAC_WILD FAC_BLACK_BOX      // feral machines in the ruins are rogue AI
extern const Faction factions[NUM_FACTIONS];   // data_game.c

// Rogue AI machines are unmanned and can be reprogrammed; a criminal's mech has
// a pilot aboard and can only be scrapped.
int factionHackable(int faction);

// ============ ECONOMY ============
#define START_CREDITS 300
#define REPAIR_INT_PER_CREDIT 3     // terminal repair: 1 credit per 3 Integrity
#define DEFEAT_FEE_PERCENT 10       // recovery fee when your mech is disabled
extern int credits;

int weaponPrice(int weapon);
int modulePrice(int module);
int chipPrice(int chip);
int chipForSale(int chip);          // Prototype / Black Box chips are never sold
int teamRepairCost(void);
int teamRepair(void);               // repairs what the credits cover, returns credits spent

// ============ PARTS ============
// Owned counts include parts installed on team mechs; "available" is what is
// left to install (same model as the chip collection). Standard refit modules
// are free and unlimited.
extern int weaponOwned[NUM_WEAPONS];
extern int moduleOwned[NUM_REFIT_MODULES];
int moduleIsStandard(int module);
int weaponAvailable(int weapon);
int moduleAvailable(int module);
void partsAddFromMech(const Mech* m);   // a new unit's weapons, modules and chips join the inventory

// ============ MANUFACTURERS ============
typedef struct {
    const char* name;
    const char* tagline;
    Color color;
} Company;

typedef struct {
    int company;
    int chassis;
    int level;          // firmware revision step it ships with
    int price;
} CatalogEntry;

enum { CO_ATLAS, CO_KESTREL, CO_HELIOS, NUM_COMPANIES };   // one showroom per zone, same order
#define NUM_CATALOG 8
extern const Company companies[NUM_COMPANIES];
extern const CatalogEntry catalog[NUM_CATALOG];

// Purchases return NULL on success or the reason they failed
const char* buyMech(int entry);
const char* buyWeapon(int weapon);
const char* buyModule(int module);
const char* buyChip(int chip);

// ============ JOBS ============
typedef enum { JOB_BOUNTY, JOB_CULL, JOB_RECOVER } JobType;
typedef enum { REWARD_NONE, REWARD_WEAPON, REWARD_MODULE, REWARD_CHIP, REWARD_MECH } RewardKind;
typedef enum { JS_OPEN, JS_ACTIVE, JS_READY, JS_DONE } JobState;

typedef struct {
    const char* title;
    const char* client;
    JobType type;
    int zone;
    const char* trainer;    // BOUNTY: encounter to defeat (by name)
    int archetype;          // RECOVER: archetype to reprogram, -1 = any
    int count;              // CULL: machines to scrap
    int credits;
    RewardKind reward;
    int rewardItem;         // weapon / module / chip / chassis index
    const char* brief;
} JobDef;

#define NUM_JOBS 12
#define MAX_ACTIVE_JOBS 3
extern const JobDef jobDefs[NUM_JOBS];
extern int jobState[NUM_JOBS];
extern int jobProgress[NUM_JOBS];

int jobsActive(void);
const char* jobAccept(int job);     // NULL on success
const char* jobClaim(int job);      // NULL on success; pays credits and the item
const char* jobObjective(int job);  // "Scrap rogue machines in SECTOR BETA  2/4"
const char* rewardName(RewardKind kind, int item);

// ============ BATTLE EVENTS ============
// Called by battle.c; each writes a short loot / job line into note.
int gameWildLevel(int zone);
void gameOnWildScrapped(const Mech* enemy, char* note, int size);
void gameOnHacked(const Mech* caught, int archetype, char* note, int size);
void gameOnEncounterDefeated(int trainer, char* note, int size);
void gameOnPlayerDisabled(char* note, int size);

// ============ SESSION ============
void gameNew(void);                 // fresh campaign: world, roster, inventory, credits, jobs
void gameChooseStarter(int starter);    // new game: replace the team with the picked starter line
int gameSave(void);                 // 1 on success
int gameLoad(void);                 // 1 on success
int gameSaveExists(void);

#endif
