#include "mech.h"

const char* platformNames[NUM_PLATFORMS] = {
    "MACHINE GUN", "SHOTGUN", "RAILGUN", "MISSILE", "FLAMETHROWER",
    "LASER", "GRENADE LAUNCHER", "ROCKET POD", "BLADE", "EMITTER",
};

const char* munitionNames[NUM_MUNITIONS] = {
    "BALLISTIC", "ENERGY", "THERMAL", "ELECTROMAGNETIC", "EXPLOSIVE", "CHEMICAL",
};

const char* targetingNames[4] = { "SINGLE", "AREA", "CONE", "LINE" };

Color munitionColor(int munition) {
    switch (munition) {
    case MUN_BALLISTIC:       return (Color) { 255, 220, 140, 255 };
    case MUN_ENERGY:          return (Color) { 100, 220, 255, 255 };
    case MUN_THERMAL:         return (Color) { 255, 130, 60, 255 };
    case MUN_ELECTROMAGNETIC: return (Color) { 170, 150, 255, 255 };
    case MUN_EXPLOSIVE:       return (Color) { 255, 170, 90, 255 };
    default:                  return (Color) { 150, 255, 110, 255 };   // chemical
    }
}

// ============ WEAPON SYSTEM RATINGS ============
// Compatibility per class: HEAVY ASSAULT, ARTILLERY, RECON, EW
const int platformRating[NUM_PLATFORMS][NUM_CLASSES] = {
    { 5, 4, 5, 3 },   // machine gun
    { 5, 1, 4, 2 },   // shotgun
    { 3, 5, 2, 2 },   // railgun
    { 4, 5, 3, 3 },   // missile
    { 5, 1, 3, 2 },   // flamethrower
    { 3, 4, 5, 5 },   // laser
    { 4, 5, 2, 2 },   // grenade launcher
    { 4, 5, 3, 2 },   // rocket pod
    { 5, 1, 5, 2 },   // blade
    { 1, 2, 3, 5 },   // emitter
};

const int munitionRating[NUM_MUNITIONS][NUM_CLASSES] = {
    { 5, 5, 4, 3 },   // ballistic
    { 3, 4, 5, 5 },   // energy
    { 5, 2, 3, 3 },   // thermal
    { 1, 2, 3, 5 },   // electromagnetic
    { 5, 5, 2, 2 },   // explosive
    { 3, 2, 4, 5 },   // chemical
};

// ============ WEAPONS ============
// Base profiles, used as-is on a 5-star mount (see weaponFit).
// Temporary numbers, scaled to the 65-200 Integrity range of the role table.
// Thermal / Chemical status effects and splash damage are not simulated yet.
const Weapon weaponTable[NUM_WEAPONS] = {
    //  name               dmg cost acc rng pen targeting      heat munition             platform               scr ammo fx
    { "MACHINE GUN",       14, 1,  90, 3, 10, TARGET_SINGLE,   8, MUN_BALLISTIC,       PLAT_MACHINE_GUN,       0,  0, FX_PULSE },
    { "SHOTGUN",           22, 1,  75, 1, 20, TARGET_CONE,    10, MUN_BALLISTIC,       PLAT_SHOTGUN,           0,  0, FX_PULSE },
    { "RAILGUN",           30, 2,  95, 5, 60, TARGET_LINE,    28, MUN_BALLISTIC,       PLAT_RAILGUN,           0,  0, FX_BEAM },
    { "AA MISSILE",        26, 2,  85, 4, 80, TARGET_SINGLE,  18, MUN_EXPLOSIVE,       PLAT_MISSILE,           0,  6, FX_MISSILE },
    { "ROCKET POD",        34, 2,  70, 3, 35, TARGET_AREA,    22, MUN_EXPLOSIVE,       PLAT_ROCKET_POD,        0,  4, FX_MISSILE },
    { "PULSE LASER",       16, 1, 100, 4,  5, TARGET_LINE,    15, MUN_ENERGY,          PLAT_LASER,             0,  0, FX_BEAM },
    { "FLAMER",            20, 1,  85, 1,  5, TARGET_CONE,    20, MUN_THERMAL,         PLAT_FLAMETHROWER,      0,  0, FX_BEAM },
    { "PLASMA BLADE",      24, 1,  90, 1, 25, TARGET_SINGLE,  12, MUN_ENERGY,          PLAT_BLADE,             0,  0, FX_BLADE },
    { "GRENADE LAUNCHER",  20, 1,  80, 3, 30, TARGET_AREA,    12, MUN_EXPLOSIVE,       PLAT_GRENADE_LAUNCHER,  0,  8, FX_MISSILE },
    { "SIEGE MORTAR",      44, 3,  70, 6, 40, TARGET_AREA,    40, MUN_EXPLOSIVE,       PLAT_GRENADE_LAUNCHER,  0,  3, FX_NOVA },
    { "ARC EMITTER",        8, 1,  95, 2,  0, TARGET_SINGLE,   8, MUN_ELECTROMAGNETIC, PLAT_EMITTER,          45,  0, FX_ARC },
    { "JAMMER",             0, 1, 100, 4,  0, TARGET_SINGLE,   5, MUN_ELECTROMAGNETIC, PLAT_EMITTER,          80,  0, FX_JAM },
    { "CORROSIVE SPRAY",   12, 1,  85, 1, 10, TARGET_CONE,    10, MUN_CHEMICAL,        PLAT_FLAMETHROWER,      0,  0, FX_PULSE },
};
