#ifndef MECH_H
#define MECH_H

#include "raylib.h"
#include "stats.h"
#include "firmware.h"

// Class -> Role -> Chassis (Model) -> Refit -> Weapon -> Munition

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
    NUM_ROLES
} MechRole;

extern const char* classNames[NUM_CLASSES];
extern const char* classInitials[NUM_CLASSES];
extern const char* roleNames[NUM_ROLES];
extern const Stats classBaseline[NUM_CLASSES];   // fallback attributes (doc 5.2)
extern const Stats roleBaseline[NUM_ROLES];      // role attributes (doc 5.3)

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
} Weapon;

enum {
    W_MACHINE_GUN, W_SHOTGUN, W_RAILGUN, W_AA_MISSILE, W_ROCKET_POD, W_PULSE_LASER, W_FLAMER,
    W_PLASMA_BLADE, W_GRENADE_LAUNCHER, W_SIEGE_MORTAR, W_ARC_EMITTER, W_JAMMER, W_CORROSIVE_SPRAY,
    NUM_WEAPONS
};
extern const Weapon weaponTable[NUM_WEAPONS];   // data_weapons.c
extern const char* platformNames[NUM_PLATFORMS];
extern const char* munitionNames[NUM_MUNITIONS];
extern const char* targetingNames[4];
Color munitionColor(int munition);

// A weapon mounted on a specific machine
typedef struct {
    int weapon;     // index into weaponTable[], -1 = empty mount
    int ammo;       // remaining uses this battle
} WeaponInstance;

// ============ MODELS ============
// Temporary model table (replaces the old Species table). Stats are the role
// baseline plus the model's delta.
typedef struct {
    const char* designation;    // "HA-D-32"
    const char* name;           // "BULWARK"
    MechRole role;
    const char* desc;
    int look;                   // procedural sprite (ui_sprites.c)
    Color body, accent, glow;
    Stats delta;
    int weapons[MAX_WEAPONS];   // stock loadout, -1 = empty
    int rarity;                 // 1 common .. 3 rare, 0 = never wild
} MechModel;

enum {
    MODEL_NOVA, MODEL_BULWARK, MODEL_WISP, MODEL_RAZOR, MODEL_HAVOC, MODEL_OBLIVION,
    MODEL_HOUND, MODEL_STATIC, MODEL_DUMMY,
    NUM_MODELS
};
extern const MechModel mechModels[NUM_MODELS];    // data_mechs.c

// ============ REFIT ============
typedef enum { SLOT_HEAD, SLOT_BODY, SLOT_ARMS, SLOT_LEGS, NUM_REFIT_SLOTS } RefitSlot;

typedef struct {
    const char* name;
    RefitSlot slot;
    const char* desc;
    Stats delta;
    int rating[NUM_CLASSES];    // compatibility 1-5 per class
} RefitModule;

enum {
    REFIT_STANDARD_OPTICS, REFIT_TARGETING_ARRAY, REFIT_EW_SUITE,
    REFIT_STANDARD_FRAME, REFIT_HEAVY_PLATING, REFIT_LIGHT_PLATING, REFIT_OVERCLOCKED_REACTOR, REFIT_CRYO_COOLING,
    REFIT_STANDARD_MOUNTS, REFIT_STABILIZED_MOUNTS, REFIT_SHIELD_ARM,
    REFIT_BIPEDAL_LEGS, REFIT_TREADS, REFIT_HOVER_SYSTEM,
    NUM_REFIT_MODULES
};
extern const int refitStandard[NUM_REFIT_SLOTS];            // stock module per slot
extern const RefitModule refitModules[NUM_REFIT_MODULES];   // data_mechs.c
extern const char* refitSlotNames[NUM_REFIT_SLOTS];
float refitRatingFactor(int rating);    // how much of a module's upside survives

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
    int model;                          // index into mechModels[]
    MechStats stats;
    WeaponInstance weapons[MAX_WEAPONS];
    int refit[NUM_REFIT_SLOTS];         // module index per slot
    Firmware fw;
} Mech;

// Every layer that feeds a mech's attributes, for the debug screen
typedef enum { LAYER_ROLE, LAYER_MODEL, LAYER_REFIT, LAYER_FIRMWARE, LAYER_CHIPS, NUM_STAT_LAYERS } StatLayer;
typedef struct {
    Stats layer[NUM_STAT_LAYERS];   // LAYER_ROLE is absolute, the rest are deltas
    Stats sum;                      // unclamped total
    Stats final;                    // clamped to attribute ranges
} StatBreakdown;
extern const char* statLayerNames[NUM_STAT_LAYERS];

Mech mechCreate(int model, int revision);
const MechModel* mechModel(const Mech* m);
MechClass mechClass(const Mech* m);
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

// ============ ROSTER ============
extern Mech team[MAX_TEAM];
extern int teamSize;
extern int activeTeamSlot;
void rosterInit(void);
int rosterAdd(const Mech* m);       // returns 0 if full
Mech* rosterActive(void);
int chipAvailable(int chip);        // owned copies not installed on any team mech

#endif
