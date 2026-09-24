#ifndef MECH_H
#define MECH_H

#include "raylib.h"
#include "stats.h"
#include "firmware.h"

// Class -> Role -> Chassis (Model) -> Refit -> Weapon -> Munition
//
// A machine is assembled by createMech from four choices plus a level:
//   Class    what it is built to do        (class baseline, refit ratings)
//   Role     how it does that job           (role baseline; ROLE_NONE = class fallback)
//   Chassis  the individual model           (stat delta, sprite, stock refit)
//   Refit    how this machine is configured (Weapon System, Head, Body, Arms, Legs)
//   Level    firmware revision step         (0 = FW 1.0)

#define MAX_TEAM 6
#define MAX_WEAPONS 4

// ============ CLASS / ROLE ============
typedef enum { CLASS_HEAVY_ASSAULT, CLASS_ARTILLERY, CLASS_RECON, CLASS_EW, NUM_CLASSES } MechClass;

// Four roles per class, in class order, so a role's class is role / ROLES_PER_CLASS
#define ROLES_PER_CLASS 4
typedef enum {
    ROLE_BREACHER, ROLE_DREADNOUGHT, ROLE_JUGGERNAUT, ROLE_IRONCLAD,
    ROLE_BOMBARD, ROLE_ORDNANCE, ROLE_ARBALEST, ROLE_BATTERY,
    ROLE_INFILTRATOR, ROLE_SKIRMISHER, ROLE_SCOUT, ROLE_PROWLER,
    ROLE_CATCHER, ROLE_DISRUPTOR, ROLE_SAPPER, ROLE_AEGIS,
    NUM_ROLES,
    ROLE_NONE = -1      // no role: the class baseline fallback is used
} MechRole;

extern const char* classNames[NUM_CLASSES];
extern const char* classInitials[NUM_CLASSES];
extern const char* roleNames[NUM_ROLES];
extern const Stats classBaseline[NUM_CLASSES];   // fallback attributes (doc 5.2)
extern const Stats roleBaseline[NUM_ROLES];      // role attributes (doc 5.3)
MechClass roleClass(MechRole role);
const char* roleName(MechRole role);             // "NONE" for ROLE_NONE

// ============ WEAPONS ============
typedef enum {
    PLAT_MACHINE_GUN, PLAT_SHOTGUN, PLAT_RAILGUN, PLAT_MISSILE, PLAT_FLAMETHROWER,
    PLAT_LASER, PLAT_GRENADE_LAUNCHER, PLAT_ROCKET_POD, PLAT_BLADE, PLAT_EMITTER,
    NUM_PLATFORMS
} WeaponPlatform;

typedef enum {
    MUN_BALLISTIC, MUN_ENERGY, MUN_THERMAL, MUN_ELECTROMAGNETIC, MUN_EXPLOSIVE, MUN_CHEMICAL,
    NUM_MUNITIONS
} Munition;

typedef enum { TARGET_SINGLE, TARGET_AREA, TARGET_CONE, TARGET_LINE } Targeting;

// Battle visual used when a weapon fires
enum { FX_NONE = 0, FX_PULSE, FX_BEAM, FX_SCAN, FX_NOVA, FX_ARC, FX_BLADE, FX_JAM, FX_MISSILE };

typedef struct {
    char name[32];
    int baseDamage;
    int energyCost;     // action points
    int accuracy;       // 0-100, weapon's base hit chance
    int range;
    int armorPen;       // 0-100, % of raw damage that bypasses Armor
    int targeting;      // Targeting
    int heat;           // heat generated per use
    int munition;       // Munition
    // prototype extras beyond the core profile
    int platform;       // WeaponPlatform
    int scramble;       // scramble strength (0 = none)
    int ammo;           // uses per battle, 0 = unlimited
    int fx;             // battle visual
    int virus;          // 1 = a scramble that lands always becomes Firmware Corruption
} Weapon;

enum {
    W_MACHINE_GUN, W_SHOTGUN, W_RAILGUN, W_AA_MISSILE, W_ROCKET_POD, W_PULSE_LASER, W_FLAMER,
    W_PLASMA_BLADE, W_GRENADE_LAUNCHER, W_SIEGE_MORTAR, W_ARC_EMITTER, W_JAMMER, W_CORROSIVE_SPRAY,
    W_VIRUS_UPLINK,
    NUM_WEAPONS
};
extern const Weapon weaponTable[NUM_WEAPONS];   // data_weapons.c
extern const char* platformNames[NUM_PLATFORMS];
extern const char* munitionNames[NUM_MUNITIONS];
extern const char* targetingNames[4];
Color munitionColor(int munition);

// A weapon mounted on a specific machine. The Weapon System is rated like any
// other refit: `fitted` is the table weapon scaled by the mount's rating, and
// is what combat reads (see mechWeapon).
typedef struct {
    int weapon;     // index into weaponTable[], -1 = empty mount
    int ammo;       // remaining uses this battle
    int rating;     // Weapon System compatibility 1-5 for this machine's class
    Weapon fitted;
} WeaponInstance;

// Weapon System compatibility: rated separately for platform and munition,
// the mount's rating is the rounded-up average of the two.
extern const int platformRating[NUM_PLATFORMS][NUM_CLASSES];   // data_weapons.c
extern const int munitionRating[NUM_MUNITIONS][NUM_CLASSES];
int weaponRating(MechClass cls, int weapon);
Weapon weaponFit(int weapon, int rating);

// ============ REFIT ============
typedef enum { SLOT_HEAD, SLOT_BODY, SLOT_ARMS, SLOT_LEGS, NUM_REFIT_SLOTS } RefitSlot;

// ============ CHASSIS (MODELS) ============
// A chassis is one model within a class role. Stats are the role baseline plus
// the chassis delta; `refit` is the configuration it ships with.
typedef struct {
    const char* designation;    // "HA-D-32"
    const char* name;           // "BULWARK"
    MechRole role;
    const char* desc;
    int look;                   // procedural sprite (ui_sprites.c)
    Color body, accent, glow;
    Stats delta;
    int weapons[MAX_WEAPONS];   // stock Weapon System, -1 = empty
    int refit[NUM_REFIT_SLOTS]; // stock Head / Body / Arms / Legs modules
    int rarity;                 // 1 common .. 3 rare, 0 = never wild
} MechModel;

enum {
    MODEL_NOVA, MODEL_BULWARK, MODEL_WISP, MODEL_RAZOR, MODEL_HAVOC, MODEL_OBLIVION,
    MODEL_HOUND, MODEL_STATIC, MODEL_LONGBOW, MODEL_DUMMY,
    NUM_MODELS
};
extern const MechModel mechModels[NUM_MODELS];    // data_mechs.c

// ============ REFIT MODULES ============
typedef struct {
    const char* name;
    RefitSlot slot;
    const char* desc;
    Stats delta;
    int rating[NUM_CLASSES];    // compatibility 1-5 per class
} RefitModule;

enum {
    REFIT_STANDARD_OPTICS, REFIT_TARGETING_ARRAY, REFIT_EW_SUITE, REFIT_LONG_RANGE_RADAR,
    REFIT_STANDARD_FRAME, REFIT_HEAVY_PLATING, REFIT_LIGHT_PLATING, REFIT_OVERCLOCKED_REACTOR, REFIT_CRYO_COOLING,
    REFIT_STANDARD_MOUNTS, REFIT_STABILIZED_MOUNTS, REFIT_SHIELD_ARM,
    REFIT_BIPEDAL_LEGS, REFIT_TREADS, REFIT_HOVER_SYSTEM, REFIT_JUMP_JETS,
    NUM_REFIT_MODULES
};
extern const int refitStandard[NUM_REFIT_SLOTS];            // standard module per slot
extern const RefitModule refitModules[NUM_REFIT_MODULES];   // data_mechs.c
extern const char* refitSlotNames[NUM_REFIT_SLOTS];
float refitRatingFactor(int rating);    // how much of a module's upside survives

// A full refit: the Weapon System plus the four module slots
typedef struct {
    int weapons[MAX_WEAPONS];           // weapon per mount, -1 = empty
    int modules[NUM_REFIT_SLOTS];       // module per slot
} Refit;
Refit chassisRefit(int chassis);        // the configuration a chassis ships with

// ============ MECH STATS ============
// The live attribute block of a machine (design doc section 3). Maximums and
// fixed attributes are rebuilt from the stat layers by mechRefreshStats; the
// current pools (integrity, armor, energy, heat) change during play.
typedef struct {
    int integrity, maxIntegrity;
    int armor, maxArmor;
    float power;            // 0.50-2.00
    int mobility;           // 0-100
    int energy, maxEnergy;  // 1-5
    int heat, maxHeat;      // 0-200
    int cooling;            // heat removed at the start of each turn (Body refit)
    int accuracy;           // 0-100
    int stability;          // 0-100
} MechStats;

// ============ MECH ============
typedef struct {
    char name[32];
    MechClass cls;
    MechRole role;                      // ROLE_NONE = class baseline fallback
    int model;                          // chassis, index into mechModels[]
    MechStats stats;
    WeaponInstance weapons[MAX_WEAPONS];
    int refit[NUM_REFIT_SLOTS];         // module index per slot
    Firmware fw;
    int boss;                           // boss rules: double Integrity, past the 200 cap
} Mech;

// Every layer that feeds a mech's attributes, for the debug screen
typedef enum { LAYER_ROLE, LAYER_MODEL, LAYER_REFIT, LAYER_FIRMWARE, LAYER_CHIPS, NUM_STAT_LAYERS } StatLayer;
typedef struct {
    Stats layer[NUM_STAT_LAYERS];   // LAYER_ROLE is absolute (class baseline if no role), the rest are deltas
    Stats sum;                      // unclamped total
    Stats final;                    // clamped to attribute ranges
} StatBreakdown;
extern const char* statLayerNames[NUM_STAT_LAYERS];

// Mech factory. A role from another class is ignored (class fallback is used);
// refit NULL = the chassis' stock refit; level = firmware revision step.
Mech createMech(MechClass cls, MechRole role, int chassis, const Refit* refit, int level);
Mech mechCreateStock(int chassis, int level);   // chassis in its own role with its stock refit
const MechModel* mechModel(const Mech* m);
MechClass mechClass(const Mech* m);
MechRole mechRole(const Mech* m);
void mechStatBreakdown(const Mech* m, StatBreakdown* out);
void mechRefreshStats(Mech* m);     // rebuild maximums/attributes after refit, firmware or chip changes
void mechRepair(Mech* m);           // restore Integrity and Armor, vent Heat
void mechReplate(Mech* m);          // restore Armor only
void mechReloadWeapons(Mech* m);
void mechSetWeapon(Mech* m, int mount, int weapon);
void mechSetRefit(Mech* m, RefitSlot slot, int module);
int refitRating(const Mech* m, int module);
const Weapon* mechWeapon(const Mech* m, int mount);   // NULL if empty mount
int mechNumWeapons(const Mech* m);

// ============ ENEMY ARCHETYPES ============
// Enemy blueprints fed through the same createMech pipeline as everything
// else, plus the weights the battle AI scores its options with.
typedef struct {
    float damage;       // weight on expected damage
    float armorBias;    // value of Armor damage relative to Integrity damage (0..1)
    float scramble;     // weight on expected scramble / corruption payloads
    float heatCaution;  // 0 = fires until the thermal limit .. 1 = keeps headroom for next turn
    float finisher;     // weight on shots that can scrap the target outright
    float desperation;  // scramble weight multiplier while below 50% Integrity
} AIProfile;
extern const AIProfile aiDefault;

typedef struct {
    const char* name;
    const char* desc;
    MechClass cls;
    MechRole role;
    int chassis;
    Refit refit;
    int chips[MAX_SOCKETS];
    int numChips;
    int trait;
    int branches[MAX_MAJORS];   // picked in this order at major revisions, the rest random
    int numBranches;
    AIProfile ai;
    int boss;                   // ignores Processing Capacity and weapon ratings; trainer-only, so never hacked
} EnemyArchetype;

enum {
    ARCH_BRAWLER, ARCH_BERSERKER, ARCH_SNIPER, ARCH_SKIRMISHER, ARCH_JAMMER, ARCH_PROWLER, ARCH_BOMBARD,
    ARCH_ORDNANCE, ARCH_OVERSEER,   // trainer-only
    NUM_ARCHETYPES
};
#define NUM_WILD_ARCHETYPES ARCH_ORDNANCE   // everything before it can roam
extern const EnemyArchetype archetypes[NUM_ARCHETYPES];   // data_mechs.c
Mech archetypeBuild(int archetype, int level);

// ============ ROSTER ============
extern Mech team[MAX_TEAM];
extern int teamSize;
extern int activeTeamSlot;
void rosterInit(void);
int rosterAdd(const Mech* m);       // returns 0 if full
Mech* rosterActive(void);
int chipAvailable(int chip);        // owned copies not installed on any team mech
int mechLoadProfile(Mech* m, int slot);   // returns chips that could not be installed, -1 if the slot is empty

#endif
