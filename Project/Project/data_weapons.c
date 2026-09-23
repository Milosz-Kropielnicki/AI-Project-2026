#include "mech.h"

const char* platformNames[NUM_PLATFORMS] = {
    "MACHINE GUN", "SHOTGUN", "RAILGUN", "MISSILE", "FLAMETHROWER",
    "LASER", "GRENADE LAUNCHER", "ROCKET POD", "BLADE", "EMITTER",
};

const char* munitionNames[NUM_MUNITIONS] = {
    "BALLISTIC", "ENERGY", "THERMAL", "ELECTROMAGNETIC", "EXPLOSIVE", "CHEMICAL",
};

Color munitionColor(Munition m) {
    switch (m) {
    case MUN_BALLISTIC:       return (Color) { 255, 220, 140, 255 };
    case MUN_ENERGY:          return (Color) { 100, 220, 255, 255 };
    case MUN_THERMAL:         return (Color) { 255, 130, 60, 255 };
    case MUN_ELECTROMAGNETIC: return (Color) { 170, 150, 255, 255 };
    case MUN_EXPLOSIVE:       return (Color) { 255, 170, 90, 255 };
    default:                  return (Color) { 150, 255, 110, 255 };   // chemical
    }
}

// Temporary numbers, scaled to the 65-200 Integrity range of the role table.
// Thermal / Chemical status effects and splash damage are not simulated yet.
const WeaponDef weaponDefs[NUM_WEAPON_DEFS] = {
    //  name              platform               munition            dmg cost acc  pen    mod  rng targeting      heat scr ammo fx
    { "MACHINE GUN",      PLAT_MACHINE_GUN,      MUN_BALLISTIC,       14, 1,  90, 0.10f, 1.0f, 3, TARGET_SINGLE,   8,  0,  0, FX_PULSE },
    { "SHOTGUN",          PLAT_SHOTGUN,          MUN_BALLISTIC,       22, 1,  75, 0.20f, 1.0f, 1, TARGET_CONE,    10,  0,  0, FX_PULSE },
    { "RAILGUN",          PLAT_RAILGUN,          MUN_BALLISTIC,       30, 2,  95, 0.60f, 1.0f, 5, TARGET_LINE,    28,  0,  0, FX_BEAM },
    { "AA MISSILE",       PLAT_MISSILE,          MUN_EXPLOSIVE,       26, 2,  85, 0.80f, 1.0f, 4, TARGET_SINGLE,  18,  0,  6, FX_MISSILE },
    { "ROCKET POD",       PLAT_ROCKET_POD,       MUN_EXPLOSIVE,       34, 2,  70, 0.35f, 1.0f, 3, TARGET_AREA,    22,  0,  4, FX_MISSILE },
    { "PULSE LASER",      PLAT_LASER,            MUN_ENERGY,          16, 1, 100, 0.05f, 1.0f, 4, TARGET_LINE,    15,  0,  0, FX_BEAM },
    { "FLAMER",           PLAT_FLAMETHROWER,     MUN_THERMAL,         20, 1,  85, 0.05f, 1.0f, 1, TARGET_CONE,    20,  0,  0, FX_BEAM },
    { "PLASMA BLADE",     PLAT_BLADE,            MUN_ENERGY,          24, 1,  90, 0.25f, 1.0f, 1, TARGET_SINGLE,  12,  0,  0, FX_BLADE },
    { "GRENADE LAUNCHER", PLAT_GRENADE_LAUNCHER, MUN_EXPLOSIVE,       20, 1,  80, 0.30f, 1.0f, 3, TARGET_AREA,    12,  0,  8, FX_MISSILE },
    { "SIEGE MORTAR",     PLAT_GRENADE_LAUNCHER, MUN_EXPLOSIVE,       44, 3,  70, 0.40f, 1.0f, 6, TARGET_AREA,    40,  0,  3, FX_NOVA },
    { "ARC EMITTER",      PLAT_EMITTER,          MUN_ELECTROMAGNETIC,  8, 1,  95, 0.00f, 1.0f, 2, TARGET_SINGLE,   8, 45,  0, FX_ARC },
    { "JAMMER",           PLAT_EMITTER,          MUN_ELECTROMAGNETIC,  0, 1, 100, 0.00f, 1.0f, 4, TARGET_SINGLE,   5, 80,  0, FX_JAM },
    { "CORROSIVE SPRAY",  PLAT_FLAMETHROWER,     MUN_CHEMICAL,        12, 1,  85, 0.10f, 1.0f, 1, TARGET_CONE,    10,  0,  0, FX_PULSE },
};
