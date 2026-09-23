#include "mech.h"

// ============ CLASSES / ROLES ============
// Column order of every Stats row below:
//   INTEGRITY  POWER  ARMOR  MOBILITY  ENERGY  HEAT  COOLING  ACCURACY  STABILITY
// Heat capacity and Cooling are not in the design tables yet; they use a
// temporary per-class value.

const char* classNames[NUM_CLASSES] = { "HEAVY ASSAULT", "ARTILLERY", "RECON", "ELECTRONIC WARFARE" };
const char* classInitials[NUM_CLASSES] = { "HA", "A", "R", "EW" };

const char* roleNames[NUM_ROLES] = {
    "BREACHER", "DREADNOUGHT", "JUGGERNAUT", "IRONCLAD",
    "BOMBARD", "ORDNANCE", "ARBALEST", "BATTERY",
    "INFILTRATOR", "SKIRMISHER", "SCOUT", "PROWLER",
    "CATCHER", "DISRUPTOR", "SAPPER", "AEGIS",
};

const Stats classBaseline[NUM_CLASSES] = {
    { { 150, 1.15f,  80, 35, 2, 120, 30, 75, 75 } },   // Heavy Assault
    { { 100, 1.20f,  35, 20, 2, 130, 30, 90, 65 } },   // Artillery
    { {  75, 0.90f,  15, 75, 3,  90, 35, 80, 60 } },   // Recon
    { {  90, 0.75f,  25, 45, 3, 100, 30, 85, 90 } },   // Electronic Warfare
};

const Stats roleBaseline[NUM_ROLES] = {
    // Heavy Assault
    { { 145, 1.30f,  70,  40, 2, 120, 30, 70,  70 } },  // Breacher
    { { 180, 1.15f, 100,  25, 2, 120, 30, 75,  85 } },  // Dreadnought
    { { 155, 1.25f,  70,  50, 2, 120, 30, 75,  75 } },  // Juggernaut
    { { 175, 0.90f, 125,  20, 2, 120, 30, 65,  90 } },  // Ironclad
    // Artillery
    { { 105, 1.30f,  40,  15, 2, 130, 30, 80,  65 } },  // Bombard
    { { 110, 1.25f,  50,  20, 2, 130, 30, 90,  70 } },  // Ordnance
    { {  80, 1.45f,  20,  25, 2, 130, 30, 98,  60 } },  // Arbalest
    { { 115, 1.00f,  40,  15, 3, 130, 30, 85,  75 } },  // Battery
    // Recon
    { {  65, 1.00f,  10,  90, 3,  90, 35, 85,  50 } },  // Infiltrator
    { {  75, 1.00f,  15, 100, 4,  90, 35, 80,  60 } },  // Skirmisher
    { {  70, 0.80f,  10,  85, 4,  90, 35, 95,  65 } },  // Scout
    { {  65, 1.20f,  10,  85, 3,  90, 35, 85,  50 } },  // Prowler
    // Electronic Warfare
    { {  85, 0.65f,  20,  45, 4, 100, 30, 95,  95 } },  // Catcher
    { {  90, 0.75f,  25,  40, 4, 100, 30, 90, 100 } },  // Disruptor
    { { 100, 0.70f,  40,  35, 3, 100, 30, 85,  90 } },  // Sapper
    { { 110, 0.60f,  50,  25, 3, 100, 30, 80,  95 } },  // Aegis
};

// ============ MODELS (temporary) ============
// Stand-ins for the old species so the prototype stays playable; the table is
// meant to be replaced by real models. Delta is added on top of the role row.
const MechModel mechModels[NUM_MODELS] = {
    { "HA-J-07", "NOVA", ROLE_JUGGERNAUT, "A balanced frontline mech.", 0,
      {60,120,200,255}, {100,200,255,255}, {80,220,255,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_PLASMA_BLADE, W_AA_MISSILE, W_RAILGUN }, 1 },
    { "HA-D-32", "BULWARK", ROLE_DREADNOUGHT, "Heavy armor, slow but sturdy.", 1,
      {60,140,90,255}, {120,220,140,255}, {140,255,160,255},
      { { 5, 0, 10, -5, 0, 0, 0, 0, 0 } },
      { W_SHOTGUN, W_MACHINE_GUN, W_GRENADE_LAUNCHER, -1 }, 1 },
    { "R-S-04", "WISP", ROLE_SKIRMISHER, "Extremely fast, fragile frame.", 2,
      {220,200,60,255}, {255,240,120,255}, {255,240,140,255},
      { { -5, 0, 0, 0, 0, 0, 5, 0, 0 } },
      { W_PULSE_LASER, W_ARC_EMITTER, W_MACHINE_GUN, -1 }, 2 },
    { "HA-B-11", "RAZOR", ROLE_BREACHER, "Blade-armed, hits hard and fast.", 3,
      {180,60,60,255}, {240,110,90,255}, {255,140,120,255},
      { { 0, 0.05f, 0, 5, 0, 0, 0, 0, 0 } },
      { W_PLASMA_BLADE, W_SHOTGUN, W_FLAMER, -1 }, 2 },
    { "A-B-21", "HAVOC", ROLE_BOMBARD, "Missile platform, high offense.", 4,
      {110,60,160,255}, {190,120,240,255}, {210,150,255,255},
      { { 0, 0, 0, 0, 0, 10, 0, 0, 0 } },
      { W_ROCKET_POD, W_AA_MISSILE, W_SIEGE_MORTAR, W_MACHINE_GUN }, 3 },
    { "A-O-66", "OBLIVION", ROLE_ORDNANCE, "Apex predator. Devastating power.", 5,
      {50,25,50,255}, {220,50,80,255}, {255,80,80,255},
      { { 15, 0.10f, 15, 0, 0, 20, 5, 0, 10 } },
      { W_RAILGUN, W_SIEGE_MORTAR, W_PULSE_LASER, W_JAMMER }, 0 },
    { "R-P-07", "HOUND", ROLE_PROWLER, "Small quadruped ambush robot.", 2,
      {40,90,90,255}, {90,220,200,255}, {120,255,220,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_SHOTGUN, W_PLASMA_BLADE, W_CORROSIVE_SPRAY, -1 }, 2 },
    { "EW-D-13", "STATIC", ROLE_DISRUPTOR, "Jamming platform that scrambles telemetry.", 0,
      {70,60,130,255}, {150,130,255,255}, {190,170,255,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_ARC_EMITTER, W_JAMMER, W_PULSE_LASER, -1 }, 2 },
};

// ============ REFIT MODULES ============
// rating = compatibility per class: HEAVY ASSAULT, ARTILLERY, RECON, EW
const char* refitSlotNames[NUM_REFIT_SLOTS] = { "HEAD", "BODY", "ARMS", "LEGS" };

const int refitStandard[NUM_REFIT_SLOTS] = {
    REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS
};

const RefitModule refitModules[NUM_REFIT_MODULES] = {
    // HEAD - sensors, targeting, radar, EW modules
    { "STANDARD OPTICS", SLOT_HEAD, "Stock sensor package.",
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }, { 5, 5, 5, 5 } },
    { "TARGETING ARRAY", SLOT_HEAD, "+8 Accuracy, -5 Stability.",
      { { 0, 0, 0, 0, 0, 0, 0, 8, -5 } }, { 3, 5, 4, 3 } },
    { "EW SUITE", SLOT_HEAD, "+15 Stability, -5 Accuracy.",
      { { 0, 0, 0, 0, 0, 0, 0, -5, 15 } }, { 2, 2, 3, 5 } },
    // BODY - armor, reactor, cooling
    { "STANDARD FRAME", SLOT_BODY, "Stock hull.",
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }, { 5, 5, 5, 5 } },
    { "HEAVY PLATING", SLOT_BODY, "+50 Armor, -15 Mobility.",
      { { 0, 0, 50, -15, 0, 0, 0, 0, 0 } }, { 5, 3, 1, 2 } },
    { "LIGHT PLATING", SLOT_BODY, "+20 Mobility, -15 Armor.",
      { { 0, 0, -15, 20, 0, 0, 0, 0, 0 } }, { 1, 2, 5, 4 } },
    { "OVERCLOCKED REACTOR", SLOT_BODY, "+1 Energy, +0.10 Power, -10 Stability.",
      { { 0, 0.10f, 0, 0, 1, 0, 0, 0, -10 } }, { 3, 3, 3, 4 } },
    { "CRYO COOLING", SLOT_BODY, "+40 Heat capacity, +15 Cooling.",
      { { 0, 0, 0, 0, 0, 40, 15, 0, 0 } }, { 4, 5, 3, 4 } },
    // ARMS - equipment interface
    { "STANDARD MOUNTS", SLOT_ARMS, "Stock hardpoints.",
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }, { 5, 5, 5, 5 } },
    { "STABILIZED MOUNTS", SLOT_ARMS, "+5 Accuracy, -5 Mobility.",
      { { 0, 0, 0, -5, 0, 0, 0, 5, 0 } }, { 4, 5, 3, 4 } },
    { "SHIELD ARM", SLOT_ARMS, "+20 Armor, -5 Accuracy.",
      { { 0, 0, 20, 0, 0, 0, 0, -5, 0 } }, { 5, 3, 2, 4 } },
    // LEGS - movement
    { "BIPEDAL LEGS", SLOT_LEGS, "Stock legs. Moderate stability.",
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }, { 5, 5, 5, 5 } },
    { "TREADS", SLOT_LEGS, "+20 Armor, +10 Stability, -15 Mobility.",
      { { 0, 0, 20, -15, 0, 0, 0, 0, 10 } }, { 5, 5, 1, 3 } },
    { "HOVER SYSTEM", SLOT_LEGS, "+20 Mobility, -10 Stability, -10 Armor.",
      { { 0, 0, -10, 20, 0, 0, 0, 0, -10 } }, { 1, 2, 5, 4 } },
};
