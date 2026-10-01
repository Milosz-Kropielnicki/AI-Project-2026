#include "mech.h"

// ============ CLASSES / ROLES ============
// Column order of every Stats row below:
//   INTEGRITY  POWER  ARMOR  MOBILITY  ENERGY  HEAT  COOLING  ACCURACY  STABILITY
// Heat capacity and Cooling are not in the design tables yet; they use a
// temporary per-class value, except for the four vertical-slice roles
// (Dreadnought, Arbalest, Skirmisher, Disruptor) which get their own thermal
// profile so they play differently.

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
    { { 180, 1.15f, 100,  25, 2, 160, 25, 75,  85 } },  // Dreadnought  - deep heat sink, slow to vent
    { { 155, 1.25f,  70,  50, 2, 120, 30, 75,  75 } },  // Juggernaut
    { { 175, 0.90f, 125,  20, 2, 120, 30, 65,  90 } },  // Ironclad
    // Artillery
    { { 105, 1.30f,  40,  15, 2, 130, 30, 80,  65 } },  // Bombard
    { { 110, 1.25f,  50,  20, 2, 130, 30, 90,  70 } },  // Ordnance
    { {  80, 1.45f,  20,  25, 2, 110, 20, 98,  60 } },  // Arbalest     - one big shot, then cool down
    { { 115, 1.00f,  40,  15, 3, 130, 30, 85,  75 } },  // Battery
    // Recon
    { {  65, 1.00f,  10,  90, 3,  90, 35, 85,  50 } },  // Infiltrator
    { {  75, 1.00f,  15, 100, 4,  70, 45, 80,  60 } },  // Skirmisher   - small, runs cool, can't soak heat
    { {  70, 0.80f,  10,  85, 4,  90, 35, 95,  65 } },  // Scout
    { {  65, 1.20f,  10,  85, 3,  90, 35, 85,  50 } },  // Prowler
    // Electronic Warfare
    { {  85, 0.65f,  20,  45, 4, 100, 30, 95,  95 } },  // Catcher
    { {  90, 0.75f,  25,  40, 4, 100, 40, 90, 100 } },  // Disruptor    - sustained jamming
    { { 100, 0.70f,  40,  35, 3, 100, 30, 85,  90 } },  // Sapper
    { { 110, 0.60f,  50,  25, 3, 100, 30, 80,  95 } },  // Aegis
};

// ============ CHASSIS (MODELS) ============
// Delta is added on top of the role row; refit is the configuration the chassis
// ships with (HEAD, BODY, ARMS, LEGS). The first entries are stand-ins for the
// old species; BULWARK, LONGBOW, WISP and STATIC are the vertical-slice builds.
#define STOCK_REFIT { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS }

const MechModel mechModels[NUM_MODELS] = {
    { "HA-J-07", "NOVA", ROLE_JUGGERNAUT, "A balanced frontline mech.", 0,
      {60,120,200,255}, {100,200,255,255}, {80,220,255,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_PLASMA_BLADE, W_AA_MISSILE, W_RAILGUN }, STOCK_REFIT, 1 },
    // Slice: Heavy Assault / Dreadnought. Maxed armor on treads, a deep heat sink.
    { "HA-D-32", "BULWARK", ROLE_DREADNOUGHT, "Heavy armor, slow but sturdy.", 1,
      {60,140,90,255}, {120,220,140,255}, {140,255,160,255},
      { { 5, 0, 10, -5, 0, 0, 0, 0, 0 } },
      { W_SHOTGUN, W_MACHINE_GUN, W_GRENADE_LAUNCHER, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_SHIELD_ARM, REFIT_TREADS }, 1 },
    // Slice: Recon / Skirmisher. Fragile, 5 actions a turn of cheap, cool weapons.
    { "R-S-04", "WISP", ROLE_SKIRMISHER, "Extremely fast, fragile frame.", 2,
      {220,200,60,255}, {255,240,120,255}, {255,240,140,255},
      { { -5, 0, 0, 0, 0, 0, 5, 0, 0 } },
      { W_PULSE_LASER, W_ARC_EMITTER, W_MACHINE_GUN, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_OVERCLOCKED_REACTOR, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS }, 2 },
    { "HA-B-11", "RAZOR", ROLE_BREACHER, "Blade-armed, hits hard and fast.", 3,
      {180,60,60,255}, {240,110,90,255}, {255,140,120,255},
      { { 0, 0.05f, 0, 5, 0, 0, 0, 0, 0 } },
      { W_PLASMA_BLADE, W_SHOTGUN, W_FLAMER, -1 }, STOCK_REFIT, 2 },
    { "A-B-21", "HAVOC", ROLE_BOMBARD, "Missile platform, high offense.", 4,
      {110,60,160,255}, {190,120,240,255}, {210,150,255,255},
      { { 0, 0, 0, 0, 0, 10, 0, 0, 0 } },
      { W_ROCKET_POD, W_AA_MISSILE, W_SIEGE_MORTAR, W_MACHINE_GUN }, STOCK_REFIT, 3 },
    { "A-O-66", "OBLIVION", ROLE_ORDNANCE, "Apex predator. Devastating power.", 5,
      {50,25,50,255}, {220,50,80,255}, {255,80,80,255},
      { { 15, 0.10f, 15, 0, 0, 20, 5, 0, 10 } },
      { W_RAILGUN, W_SIEGE_MORTAR, W_PULSE_LASER, W_JAMMER },
      { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS }, 0 },
    { "R-P-07", "HOUND", ROLE_PROWLER, "Small quadruped ambush robot.", 2,
      {40,90,90,255}, {90,220,200,255}, {120,255,220,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_SHOTGUN, W_PLASMA_BLADE, W_CORROSIVE_SPRAY, -1 }, STOCK_REFIT, 2 },
    // Slice: Electronic Warfare / Disruptor. Hovering jammer with a spare action.
    { "EW-D-13", "STATIC", ROLE_DISRUPTOR, "Jamming platform that scrambles telemetry.", 0,
      {70,60,130,255}, {150,130,255,255}, {190,170,255,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_ARC_EMITTER, W_JAMMER, W_PULSE_LASER, -1 },
      { REFIT_LONG_RANGE_RADAR, REFIT_OVERCLOCKED_REACTOR, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 2 },
    // Slice: Artillery / Arbalest. Railgun sniper braced on treads.
    { "A-A-09", "LONGBOW", ROLE_ARBALEST, "Kinetic sniper that hunts weak points.", 4,
      {90,100,120,255}, {240,170,70,255}, {255,200,110,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_RAILGUN, W_PULSE_LASER, W_MACHINE_GUN, -1 },
      { REFIT_LONG_RANGE_RADAR, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS }, 3 },
    // Test Range target: unarmed, never appears in the wild.
    // Ironclad + delta = INT 200, ARM 100, MOB 25, STB 60.
    { "TR-00", "TARGET DUMMY", ROLE_IRONCLAD, "Unarmed test-range target. Rebuilds itself.", 1,
      {90,95,110,255}, {230,200,90,255}, {255,230,120,255},
      { { 25, 0, -25, 5, 0, 0, 0, 0, -30 } },
      { -1, -1, -1, -1 }, STOCK_REFIT, 0 },
    // Heavy Assault / Ironclad: the anchor. Tower shield and plating; it draws
    // fire (Ironclads gain extra Threat every round on the field) and soaks it.
    { "HA-I-40", "RAMPART", ROLE_IRONCLAD, "Tower-shield anchor that draws enemy fire.", 6,
      {70,90,120,255}, {200,210,230,255}, {150,220,255,255},
      { { 10, 0, 15, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_HEAVY_PLATING, REFIT_SHIELD_ARM, REFIT_TREADS }, 2 },
    // EW / Catcher: paints whatever it shoots at. Its Targeting Link gives an
    // Artillery partner +15 Accuracy and crits against the mark. The delta
    // makes it a frame worth fielding (the Catcher role alone is paper-thin).
    { "EW-C-05", "SNARE", ROLE_CATCHER, "Target-painting frame. Marks prey for its artillery.", 7,
      {50,90,80,255}, {120,255,170,255}, {170,255,200,255},
      { { 35, 0.35f, 30, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_ARC_EMITTER, W_PULSE_LASER, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_STANDARD_FRAME, REFIT_STABILIZED_MOUNTS, REFIT_HOVER_SYSTEM }, 2 },
    // EW / Aegis: projects a shield over a linked Heavy Assault and soaks a
    // fifth of every hit it takes.
    { "EW-A-21", "HALO", ROLE_AEGIS, "Shield projector. Takes a share of every hit on its heavy.", 8,
      {60,80,130,255}, {130,200,255,255}, {170,230,255,255},
      { { 35, 0.35f, 30, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_ARC_EMITTER, W_CORROSIVE_SPRAY, -1 },
      { REFIT_EW_SUITE, REFIT_HEAVY_PLATING, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 2 },
    // Recon / Scout: spots targets; its Spotter Link lets an Artillery partner
    // reach the spotted mech past the front line.
    { "R-SC-12", "VANTAGE", ROLE_SCOUT, "Sensor-mast spotter. Calls shots past the front line.", 9,
      {120,110,60,255}, {255,210,90,255}, {255,230,140,255},
      { { 15, 0.10f, 10, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_PULSE_LASER, W_ARC_EMITTER, -1 },
      { REFIT_LONG_RANGE_RADAR, REFIT_LIGHT_PLATING, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS }, 2 },
    // ================= ROLE LINE-UP: two more chassis per role =================
    // Delta: INT, PWR, ARM, MOB, EN, HEAT, COOL, ACC, STB on top of the role row.
    // Weak support roles (Catcher, Aegis, Scout, Sapper) carry a sturdier delta,
    // as SNARE / HALO / VANTAGE do, so they are worth fielding.

    // ---- Heavy Assault / Breacher
    { "HA-B-19", "WRECKER", ROLE_BREACHER, "Demolition frame with a hydraulic ram; opens armor at point-blank.", 10,
      {120,100,60,255}, {255,190,70,255}, {255,220,120,255},
      { { 5, 0.05f, 10, -5, 0, 0, 0, 0, 5 } },
      { W_PLASMA_BLADE, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_STABILIZED_MOUNTS, REFIT_BIPEDAL_LEGS }, 2,
      "HYDRAULIC RAM", "First hit on an armored target strips +40% Armor.", CFX_ARMOR_BREACH, 0.40f },
    { "HA-B-25", "CINDER", ROLE_BREACHER, "Thermal breacher: plasma cutter and slag thrower.", 3,
      {110,40,30,255}, {255,120,50,255}, {255,170,90,255},
      { { 0, 0, 0, 0, 0, 20, 5, 0, 0 } },
      { W_PLASMA_BLADE, W_FLAMER, W_CORROSIVE_SPRAY, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS }, 2 },
    // ---- Heavy Assault / Dreadnought
    { "HA-D-08", "CITADEL", ROLE_DREADNOUGHT, "Layered fortress frame; the archetypal slow wall.", 1,
      {80,90,100,255}, {170,190,210,255}, {200,230,255,255},
      { { 15, -0.05f, 25, -15, 0, 0, 0, 0, 5 } },
      { W_MACHINE_GUN, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_SHIELD_ARM, REFIT_TREADS }, 1,
      "LAYERED HULL", "All incoming damage -10%.", CFX_DAMAGE_REDUCTION, 0.10f },
    { "HA-D-51", "FORTRESS", ROLE_DREADNOUGHT, "Heavy gun platform; trades mobility for sustained fire.", 4,
      {70,80,60,255}, {200,200,120,255}, {240,240,150,255},
      { { 0, 0.05f, 0, -15, 0, 0, 10, 0, 0 } },
      { W_MACHINE_GUN, W_AA_MISSILE, W_ROCKET_POD, W_SHOTGUN },
      { REFIT_TARGETING_ARRAY, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS }, 2 },
    // ---- Heavy Assault / Juggernaut
    { "HA-J-22", "RHINO", ROLE_JUGGERNAUT, "Charge-focused shock frame; bonus damage on the first attack of a charge.", 1,
      {90,70,60,255}, {230,150,100,255}, {255,190,140,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_PLASMA_BLADE, W_SHOTGUN, W_MACHINE_GUN, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS }, 2,
      "CHARGE", "Its first attack each turn deals +20% damage.", CFX_OPENER, 0.20f },
    { "HA-J-36", "BRUTE", ROLE_JUGGERNAUT, "Aggressive close-range brawler; no defensive tricks.", 3,
      {130,50,50,255}, {255,90,80,255}, {255,130,110,255},
      { { 0, 0.15f, -20, 0, 0, 0, 0, 0, -10 } },
      { W_SHOTGUN, W_PLASMA_BLADE, W_FLAMER, -1 }, STOCK_REFIT, 1 },
    // ---- Heavy Assault / Ironclad
    { "HA-I-14", "REDOUBT", ROLE_IRONCLAD, "Defensive anchor; cheaper Provocation, lower damage.", 6,
      {60,80,110,255}, {180,200,240,255}, {140,200,255,255},
      { { 10, -0.10f, 10, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_HEAVY_PLATING, REFIT_SHIELD_ARM, REFIT_TREADS }, 2,
      "BUILT-IN TAUNT", "PROVOKE without the chip, for 0 Energy.", CFX_PROVOCATION, 50, CFX_PROVOKE_DISCOUNT, 1 },
    { "HA-I-52", "GRANITE", ROLE_IRONCLAD, "Unmovable; the highest Stability in the class.", 1,
      {100,100,95,255}, {200,195,180,255}, {230,225,210,255},
      { { 5, 0, 5, -10, 0, 0, 0, 0, 10 } },
      { W_MACHINE_GUN, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_HEAVY_PLATING, REFIT_STANDARD_MOUNTS, REFIT_TREADS }, 2,
      "UNMOVABLE", "+25 Stability against scrambles.", CFX_COUNTER_INTRUSION, 25 },

    // ---- Artillery / Bombard
    { "A-B-08", "CATAPULT", ROLE_BOMBARD, "Classic siege frame; huge arcs, tiny direct fire.", 4,
      {100,80,50,255}, {230,180,90,255}, {255,210,130,255},
      { { 0, 0.05f, 0, 0, 0, 10, 0, -10, 0 } },
      { W_SIEGE_MORTAR, W_GRENADE_LAUNCHER, W_ROCKET_POD, -1 }, STOCK_REFIT, 1 },
    { "A-B-30", "TEMPEST", ROLE_BOMBARD, "Heavy barrage; rapid mortar salvoes.", 4,
      {60,70,110,255}, {140,160,255,255}, {180,200,255,255},
      { { 0, -0.25f, 0, 0, 1, 0, 5, 0, 0 } },
      { W_GRENADE_LAUNCHER, W_SIEGE_MORTAR, W_MACHINE_GUN, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_STANDARD_MOUNTS, REFIT_TREADS }, 2 },
    // ---- Artillery / Ordnance
    { "A-O-12", "LANCE", ROLE_ORDNANCE, "Penetrating strike frame; long, narrow, brutal.", 5,
      {40,50,70,255}, {120,200,255,255}, {160,230,255,255},
      { { 0, 0.15f, 0, 0, 0, 0, 0, 0, 0 } },
      { W_RAILGUN, W_PULSE_LASER, W_MACHINE_GUN, -1 },
      { REFIT_LONG_RANGE_RADAR, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_BIPEDAL_LEGS }, 3,
      "PENETRATOR", "+15% Armor Penetration on every attack.", CFX_PEN_BONUS, 15 },
    { "A-O-25", "REAPER", ROLE_ORDNANCE, "Armor-killer; bonus vs targets with >50 Armor.", 5,
      {50,40,40,255}, {200,200,200,255}, {255,90,90,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_RAILGUN, W_SIEGE_MORTAR, W_MACHINE_GUN, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_STANDARD_MOUNTS, REFIT_TREADS }, 3,
      "ARMOR KILLER", "+30% penetration vs targets with over 50 Armor.", CFX_PEN_VS_ARMORED, 0.30f },
    // ---- Artillery / Arbalest
    { "A-A-18", "BALLISTA", ROLE_ARBALEST, "Classic sniper frame; cheap, reliable, precise.", 0,
      {90,90,80,255}, {220,200,150,255}, {250,230,180,255},
      { { 10, -0.10f, 10, 0, 0, 0, 5, 0, 0 } },
      { W_RAILGUN, W_MACHINE_GUN, -1, -1 },
      { REFIT_LONG_RANGE_RADAR, REFIT_STANDARD_FRAME, REFIT_STABILIZED_MOUNTS, REFIT_TREADS }, 1 },
    { "A-A-27", "NEEDLE", ROLE_ARBALEST, "Precision platform; the highest Accuracy in the class.", 0,
      {200,200,210,255}, {120,240,255,255}, {180,250,255,255},
      { { -5, -0.05f, 0, 0, 0, 0, 0, 2, 0 } },
      { W_RAILGUN, W_PULSE_LASER, W_MACHINE_GUN, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS }, 3,
      "TRUE AIM", "+10 Accuracy, even past 100.", CFX_TRUE_AIM, 10 },
    // ---- Artillery / Battery
    { "A-BA-05", "VULCAN", ROLE_BATTERY, "Rotary autocannon; sustained single-target fire.", 11,
      {80,60,50,255}, {255,160,60,255}, {255,200,110,255},
      { { 0, 0.05f, 0, 0, 0, 20, 10, 0, 0 } },
      { W_MACHINE_GUN, W_AA_MISSILE, W_PULSE_LASER, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS }, 2 },
    { "A-BA-31", "PELTER", ROLE_BATTERY, "Light kinetic saturation; cheap and cool.", 4,
      {70,90,70,255}, {170,230,150,255}, {200,255,180,255},
      { { -10, -0.05f, 0, 10, 0, 0, 10, 0, 0 } },
      { W_MACHINE_GUN, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 }, STOCK_REFIT, 1 },

    // ---- Recon / Infiltrator
    { "R-I-03", "WRAITH", ROLE_INFILTRATOR, "Stealth-coated; bypasses sensor arrays.", 2,
      {40,40,60,255}, {150,120,255,255}, {190,170,255,255},
      { { 10, 0.05f, 5, 0, 0, 0, 0, 0, 0 } },
      { W_PULSE_LASER, W_PLASMA_BLADE, W_MACHINE_GUN, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_LIGHT_PLATING, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS }, 3,
      "INFILTRATION", "Its single-target and line shots reach past cover.", CFX_IGNORE_COVER, 1 },
    { "R-I-11", "SHADE", ROLE_INFILTRATOR, "Shadow frame; low signature, low profile.", 2,
      {30,35,40,255}, {110,140,160,255}, {150,190,210,255},
      { { 10, 0, 5, 0, 0, 0, 0, 0, 0 } },
      { W_PULSE_LASER, W_ARC_EMITTER, W_MACHINE_GUN, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 2,
      "LOW SIGNATURE", "The first attack on it each round: -25% to hit.", CFX_TARGETING_SPOOF, 0.25f },
    // ---- Recon / Skirmisher
    { "R-S-14", "DART", ROLE_SKIRMISHER, "Fast attack frame; cheap, cool, quick.", 2,
      {200,90,60,255}, {255,170,110,255}, {255,210,150,255},
      { { -5, -0.05f, 0, 0, 0, 0, 10, 0, 0 } },
      { W_MACHINE_GUN, W_PULSE_LASER, -1, -1 }, STOCK_REFIT, 1 },
    { "R-S-31", "TALON", ROLE_SKIRMISHER, "Sharp strike; bonus on the first attack after repositioning.", 3,
      {60,110,120,255}, {120,240,230,255}, {170,255,240,255},
      { { 0, 0.05f, 0, 0, -1, 0, 0, 0, 0 } },
      { W_PLASMA_BLADE, W_PULSE_LASER, W_MACHINE_GUN, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS }, 2,
      "STRIKE AND FADE", "First attack after it moves this turn: +25% damage.", CFX_REPOSITION_STRIKE, 0.25f },
    // ---- Recon / Scout
    { "R-SC-05", "PICKET", ROLE_SCOUT, "Forward observer; cheap spotter.", 7,
      {110,120,70,255}, {220,240,120,255}, {240,255,160,255},
      { { 10, 0.15f, 10, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_PULSE_LASER, -1, -1 }, STOCK_REFIT, 1 },
    { "R-SC-18", "PROBE", ROLE_SCOUT, "Recon frame; fast-locking target system.", 9,
      {70,90,110,255}, {140,220,255,255}, {190,240,255,255},
      { { 15, 0.15f, 10, 0, 0, 0, 0, -10, 0 } },
      { W_MACHINE_GUN, W_PULSE_LASER, W_ARC_EMITTER, -1 },
      { REFIT_LONG_RANGE_RADAR, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS }, 2,
      "FAST LOCK", "+20 Accuracy on its first attack each turn.", CFX_PRECISION_STRIKE, 20 },
    // ---- Recon / Prowler
    { "R-P-16", "STALKER", ROLE_PROWLER, "Hunter; tracks and finishes wounded targets.", 2,
      {60,70,50,255}, {170,210,90,255}, {210,240,130,255},
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
      { W_SHOTGUN, W_PLASMA_BLADE, W_MACHINE_GUN, -1 }, STOCK_REFIT, 2,
      "FINISHER", "+25% damage vs targets below half Integrity.", CFX_EXECUTE, 0.25f },
    { "R-P-33", "PANTHER", ROLE_PROWLER, "Silent predator; shotgun and blade burst.", 3,
      {30,30,40,255}, {90,200,170,255}, {130,240,200,255},
      { { 0, -0.05f, 0, 0, 0, 0, 0, 0, 0 } },
      { W_SHOTGUN, W_PLASMA_BLADE, -1, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_LIGHT_PLATING, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS }, 2 },

    // ---- Electronic Warfare / Catcher
    { "EW-C-12", "RELAY", ROLE_CATCHER, "Signal frame; cheaper LINK action.", 7,
      {60,100,90,255}, {130,255,200,255}, {180,255,220,255},
      { { 30, 0.30f, 25, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_ARC_EMITTER, W_PULSE_LASER, -1 },
      { REFIT_TARGETING_ARRAY, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 2,
      "QUICK UPLINK", "LINK costs 0 Energy.", CFX_LINK_DISCOUNT, 1 },
    { "EW-C-19", "NEXUS", ROLE_CATCHER, "Hub; extends link range/effect.", 7,
      {50,60,90,255}, {150,180,255,255}, {200,215,255,255},
      { { 35, 0.35f, 30, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_ARC_EMITTER, W_PULSE_LASER, -1 },
      { REFIT_EW_SUITE, REFIT_STANDARD_FRAME, REFIT_STABILIZED_MOUNTS, REFIT_HOVER_SYSTEM }, 3,
      "LINK HUB", "The link it starts is 50% stronger.", CFX_LINK_BOOST, 0.50f },
    // ---- Electronic Warfare / Disruptor
    { "EW-D-06", "SCREAMER", ROLE_DISRUPTOR, "Jamming platform; widest scramble arc.", 0,
      {90,50,90,255}, {255,120,220,255}, {255,170,240,255},
      { { 0, -0.05f, 0, 0, 0, 0, 0, 0, 0 } },
      { W_JAMMER, W_ARC_EMITTER, W_PULSE_LASER, -1 },
      { REFIT_EW_SUITE, REFIT_OVERCLOCKED_REACTOR, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 3,
      "WIDE BAND", "Its single-target scramblers hit like a cone.", CFX_SCRAMBLE_ARC, 1 },
    { "EW-D-44", "JOLT", ROLE_DISRUPTOR, "Electric disruption; cheap, fast, low damage.", 0,
      {60,80,120,255}, {120,220,255,255}, {170,240,255,255},
      { { 15, 0, 10, 20, 1, 0, 10, 0, 0 } },
      { W_ARC_EMITTER, W_JAMMER, W_MACHINE_GUN, -1 }, STOCK_REFIT, 1 },
    // ---- Electronic Warfare / Sapper
    { "EW-SA-04", "BRAMBLE", ROLE_SAPPER, "Hazard deployer; area denial.", 6,
      {80,90,40,255}, {200,230,80,255}, {230,255,120,255},
      { { 25, 0.30f, 20, 0, 0, 0, 0, 0, 0 } },
      { W_GRENADE_LAUNCHER, W_CORROSIVE_SPRAY, W_MACHINE_GUN, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_SHIELD_ARM, REFIT_BIPEDAL_LEGS }, 2,
      "HAZARD FIELD", "Area/cone hits leave 8 damage for their next turn.", CFX_HAZARD, 8 },
    { "EW-SA-11", "THORN", ROLE_SAPPER, "Area denial; strips enemy mobility.", 6,
      {70,60,40,255}, {230,160,80,255}, {255,200,120,255},
      { { 25, 0.30f, 20, 0, 0, 0, 0, 0, 0 } },
      { W_CORROSIVE_SPRAY, W_GRENADE_LAUNCHER, W_MACHINE_GUN, -1 },
      { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_SHIELD_ARM, REFIT_BIPEDAL_LEGS }, 2,
      "SNARE FIELD", "Mechs it hits lose 20 Mobility until their next turn.", CFX_SLOW, 20 },
    // ---- Electronic Warfare / Aegis
    { "EW-A-08", "PALLADIUM", ROLE_AEGIS, "Protection frame; cheaper Defense Link.", 8,
      {150,150,160,255}, {230,230,240,255}, {200,240,255,255},
      { { 35, 0.35f, 30, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_ARC_EMITTER, W_CORROSIVE_SPRAY, -1 },
      { REFIT_EW_SUITE, REFIT_HEAVY_PLATING, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 2,
      "QUICK UPLINK", "LINK costs 0 Energy.", CFX_LINK_DISCOUNT, 1 },
    { "EW-A-16", "WARD", ROLE_AEGIS, "Shield projector; modest stats, strong link.", 8,
      {60,90,140,255}, {120,190,255,255}, {170,220,255,255},
      { { 25, 0.25f, 20, 0, 0, 0, 0, 0, 0 } },
      { W_MACHINE_GUN, W_ARC_EMITTER, W_CORROSIVE_SPRAY, -1 },
      { REFIT_EW_SUITE, REFIT_HEAVY_PLATING, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM }, 2,
      "STRONG LINK", "Its Defense Link takes 30% of each hit, not 20%.", CFX_LINK_BOOST, 0.50f },
};

// ============ REFIT MODULES ============
// rating = compatibility per class: HEAVY ASSAULT, ARTILLERY, RECON, EW
const char* refitSlotNames[NUM_REFIT_SLOTS] = { "HEAD", "BODY", "ARMS", "LEGS" };

const int refitStandard[NUM_REFIT_SLOTS] = STOCK_REFIT;

const RefitModule refitModules[NUM_REFIT_MODULES] = {
    // HEAD - sensors, targeting, radar, EW modules
    { "STANDARD OPTICS", SLOT_HEAD, "Stock sensor package.",
      { { 0, 0, 0, 0, 0, 0, 0, 0, 0 } }, { 5, 5, 5, 5 } },
    { "TARGETING ARRAY", SLOT_HEAD, "+8 Accuracy, -5 Stability.",
      { { 0, 0, 0, 0, 0, 0, 0, 8, -5 } }, { 3, 5, 4, 3 } },
    { "EW SUITE", SLOT_HEAD, "+15 Stability, -5 Accuracy.",
      { { 0, 0, 0, 0, 0, 0, 0, -5, 15 } }, { 2, 2, 3, 5 } },
    { "LONG-RANGE RADAR", SLOT_HEAD, "+4 Accuracy, +8 Stability, -10 Heat capacity.",
      { { 0, 0, 0, 0, 0, -10, 0, 4, 8 } }, { 3, 5, 4, 4 } },
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
    { "JUMP JETS", SLOT_LEGS, "+10 Mobility, -5 Armor, -15 Heat capacity.",
      { { 0, 0, -5, 10, 0, -15, 0, 0, 0 } }, { 2, 1, 5, 3 } },
};

// ============ ENEMY ARCHETYPES ============
//                                  damage armorBias scramble heatCaution finisher desperation
const AIProfile aiDefault = {       1.0f,  0.6f,     0.6f,    0.4f,       1.0f,    1.0f };

const EnemyArchetype archetypes[NUM_ARCHETYPES] = {
    // Soaks the first hit, cracks armor with the breach routine, repairs itself when low.
    { "BRAWLER", "Armored Dreadnought. Hits hard up close and refuses to go down.",
      CLASS_HEAVY_ASSAULT, ROLE_DREADNOUGHT, MODEL_BULWARK,
      { { W_PLASMA_BLADE, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
        { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS } },
      { CHIP_PRECISION_STRIKE, CHIP_EMERGENCY_REPAIR, CHIP_PREDICTIVE_TARGETING }, 3,
      TRAIT_DEFENSIVE, { BRANCH_BASTION }, 1,
      { 1.0f, 0.8f, 0.0f, 0.2f, 1.0f, 1.0f }, 0 },
    // All-in melee: ignores heat, gets stronger as it breaks.
    { "BERSERKER", "Blade-armed Breacher. Overheats itself chasing the kill.",
      CLASS_HEAVY_ASSAULT, ROLE_BREACHER, MODEL_RAZOR,
      { { W_PLASMA_BLADE, W_FLAMER, W_SHOTGUN, -1 },
        { REFIT_STANDARD_OPTICS, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS } },
      { CHIP_OVERCHARGE, CHIP_EMERGENCY_POWER }, 2,
      TRAIT_AGGRESSIVE, { BRANCH_OVERCLOCK }, 1,
      { 1.3f, 0.5f, 0.0f, 0.0f, 2.0f, 1.0f }, 0 },
    // Free first railgun each turn; a second one builds heat until it has to switch or vent.
    { "SNIPER", "Arbalest on treads. Saves its heat for one armor-piercing railgun shot.",
      CLASS_ARTILLERY, ROLE_ARBALEST, MODEL_LONGBOW,
      { { W_RAILGUN, W_PULSE_LASER, W_MACHINE_GUN, -1 },
        { REFIT_LONG_RANGE_RADAR, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS } },
      { CHIP_PRECISION_STRIKE, CHIP_ARMOR_ANALYSIS, CHIP_TARGETING_SPOOF }, 3,
      TRAIT_EFFICIENT, { BRANCH_HUNTER }, 1,
      { 1.0f, 0.3f, 0.0f, 0.9f, 1.5f, 1.0f }, 0 },
    // Five cheap shots a turn and hard to pin down.
    { "SKIRMISHER", "Hit-and-run Recon frame. Many small shots, very hard to hit.",
      CLASS_RECON, ROLE_SKIRMISHER, MODEL_WISP,
      { { W_PULSE_LASER, W_MACHINE_GUN, W_ARC_EMITTER, -1 },
        { REFIT_TARGETING_ARRAY, REFIT_STANDARD_FRAME, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS } },
      { CHIP_EVASIVE_MANEUVER, CHIP_PREDICTIVE_TARGETING }, 2,
      TRAIT_AGGRESSIVE, { BRANCH_GHOST }, 1,
      { 1.0f, 0.6f, 0.4f, 0.5f, 1.0f, 1.0f }, 0 },
    // Barely damages you; scrambles your turn and corrupts your firmware instead.
    { "JAMMER", "Disruptor. Attacks your Stability and your firmware, not your armor.",
      CLASS_EW, ROLE_DISRUPTOR, MODEL_STATIC,
      { { W_VIRUS_UPLINK, W_JAMMER, W_ARC_EMITTER, W_PULSE_LASER },
        { REFIT_LONG_RANGE_RADAR, REFIT_OVERCLOCKED_REACTOR, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM } },
      { CHIP_COUNTER_INTRUSION, CHIP_EVASIVE_MANEUVER, CHIP_SYSTEM_RECOVERY }, 3,
      TRAIT_ADAPTIVE, { BRANCH_SIGNAL }, 1,
      { 1.0f, 0.6f, 1.2f, 0.5f, 1.5f, 1.5f }, 0 },
    // Ambusher: burst damage up close, dives in for the kill.
    { "PROWLER", "Quadruped ambush Recon. Shotgun and blade bursts, hunts damaged targets.",
      CLASS_RECON, ROLE_PROWLER, MODEL_HOUND,
      { { W_SHOTGUN, W_PLASMA_BLADE, W_CORROSIVE_SPRAY, -1 },
        { REFIT_STANDARD_OPTICS, REFIT_LIGHT_PLATING, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS } },
      { CHIP_PRECISION_STRIKE, CHIP_EMERGENCY_EVASION }, 2,
      TRAIT_AGGRESSIVE, { BRANCH_HUNTER }, 1,
      { 1.2f, 0.5f, 0.0f, 0.3f, 2.5f, 1.0f }, 0 },
    // Limited-ammo salvos: empties its missiles and mortars, then falls back to the MG.
    { "BOMBARD", "Missile Bombard. Heavy explosive salvos until the racks run dry.",
      CLASS_ARTILLERY, ROLE_BOMBARD, MODEL_HAVOC,
      { { W_ROCKET_POD, W_AA_MISSILE, W_SIEGE_MORTAR, W_MACHINE_GUN },
        { REFIT_LONG_RANGE_RADAR, REFIT_OVERCLOCKED_REACTOR, REFIT_SHIELD_ARM, REFIT_TREADS } },
      { CHIP_PRECISION_STRIKE, CHIP_ARMOR_ANALYSIS, CHIP_EMERGENCY_REPAIR }, 3,
      TRAIT_DEFENSIVE, { BRANCH_SIEGE }, 1,
      { 1.0f, 0.7f, 0.0f, 0.6f, 1.2f, 1.0f }, 0 },
    // Ironclad anchor: provokes your single-target fire onto its tower shield
    // so the rest of the squad can work, and repairs itself when low.
    { "GUARDIAN", "Ironclad bulwark. Provokes your fire so its squad can work.",
      CLASS_HEAVY_ASSAULT, ROLE_IRONCLAD, MODEL_RAMPART,
      { { W_MACHINE_GUN, W_SHOTGUN, W_GRENADE_LAUNCHER, -1 },
        { REFIT_STANDARD_OPTICS, REFIT_HEAVY_PLATING, REFIT_SHIELD_ARM, REFIT_TREADS } },
      { CHIP_PROVOCATION, CHIP_EMERGENCY_REPAIR, CHIP_HARDENED_KERNEL }, 3,
      TRAIT_DEFENSIVE, { BRANCH_BASTION }, 1,
      { 0.9f, 0.8f, 0.0f, 0.3f, 1.0f, 1.0f }, 0 },
    // Catcher: paints targets with cheap accurate fire; with a Sniper or Bombard
    // in the pack its Targeting Link makes their shots land and crit.
    { "MARKER", "Catcher. Paints you so its artillery can't miss.",
      CLASS_EW, ROLE_CATCHER, MODEL_SNARE,
      { { W_MACHINE_GUN, W_ARC_EMITTER, W_PULSE_LASER, -1 },
        { REFIT_TARGETING_ARRAY, REFIT_STANDARD_FRAME, REFIT_STABILIZED_MOUNTS, REFIT_HOVER_SYSTEM } },
      { CHIP_PRECISION_STRIKE, CHIP_PREDICTIVE_TARGETING }, 2,
      TRAIT_EFFICIENT, { BRANCH_SIGNAL }, 1,
      { 1.0f, 0.6f, 0.6f, 0.5f, 1.0f, 1.0f }, 0 },
    // Aegis: shields a Heavy Assault through its Defense Link.
    { "SHIELDER", "Aegis. Projects a shield over its pack's heavy.",
      CLASS_EW, ROLE_AEGIS, MODEL_HALO,
      { { W_MACHINE_GUN, W_ARC_EMITTER, W_CORROSIVE_SPRAY, -1 },
        { REFIT_EW_SUITE, REFIT_HEAVY_PLATING, REFIT_STANDARD_MOUNTS, REFIT_HOVER_SYSTEM } },
      { CHIP_HARDENED_KERNEL, CHIP_EMERGENCY_REPAIR }, 2,
      TRAIT_DEFENSIVE, { BRANCH_BASTION }, 1,
      { 0.9f, 0.7f, 0.6f, 0.5f, 1.0f, 1.0f }, 0 },
    // Scout: spots for artillery; with a Sniper or Bombard in the pack your rear
    // line isn't safe.
    { "SPOTTER", "Scout. Calls artillery fire past your front line.",
      CLASS_RECON, ROLE_SCOUT, MODEL_VANTAGE,
      { { W_MACHINE_GUN, W_PULSE_LASER, W_ARC_EMITTER, -1 },
        { REFIT_LONG_RANGE_RADAR, REFIT_LIGHT_PLATING, REFIT_STANDARD_MOUNTS, REFIT_JUMP_JETS } },
      { CHIP_EVASIVE_MANEUVER, CHIP_PREDICTIVE_TARGETING }, 2,
      TRAIT_EFFICIENT, { BRANCH_GHOST }, 1,
      { 1.0f, 0.6f, 0.4f, 0.5f, 1.0f, 1.0f }, 0 },
    // Trainer-only armor cracker on the OBLIVION chassis.
    { "ORDNANCE", "Heavy gun platform. Strips armor, then jams what is left.",
      CLASS_ARTILLERY, ROLE_ORDNANCE, MODEL_OBLIVION,
      { { W_RAILGUN, W_SIEGE_MORTAR, W_PULSE_LASER, W_JAMMER },
        { REFIT_STANDARD_OPTICS, REFIT_CRYO_COOLING, REFIT_STANDARD_MOUNTS, REFIT_BIPEDAL_LEGS } },
      { CHIP_ARMOR_ANALYSIS, CHIP_ARMOR_BREACH, CHIP_COUNTER_INTRUSION }, 3,
      TRAIT_ADAPTIVE, { BRANCH_SIEGE }, 1,
      { 1.0f, 0.9f, 0.6f, 0.5f, 1.5f, 1.0f }, 0 },
    // BOSS: misses make it deadlier, killing it triggers one last free shot, and
    // once below half Integrity it turns to corrupting your firmware.
    { "FACTORY OVERSEER", "Ancient Ordnance platform running Black Box firmware.",
      CLASS_ARTILLERY, ROLE_ORDNANCE, MODEL_OBLIVION,
      { { W_RAILGUN, W_SIEGE_MORTAR, W_VIRUS_UPLINK, W_MACHINE_GUN },
        { REFIT_LONG_RANGE_RADAR, REFIT_CRYO_COOLING, REFIT_STABILIZED_MOUNTS, REFIT_TREADS } },
      { CHIP_RECURSIVE_TARGETING, CHIP_DEAD_MAN, CHIP_ARMOR_ANALYSIS, CHIP_COUNTER_INTRUSION, CHIP_EMERGENCY_REPAIR,
        CHIP_COOLANT_DUMP }, 6,
      TRAIT_ADAPTIVE, { BRANCH_SIEGE, BRANCH_OVERCLOCK }, 2,
      { 1.0f, 0.5f, 1.0f, 0.6f, 2.0f, 3.5f }, 1 },
};
