#include "game.h"
#include "world.h"

// ============ FACTIONS ============
const Faction factions[NUM_FACTIONS] = {
    { "IRON LEGION",      FKIND_CRIMINAL, { 255, 180, 60, 255 },
      "Mercenary cartel running piloted heavy mechs across every sector." },
    { "CHROME SYNDICATE", FKIND_CRIMINAL, { 120, 220, 255, 255 },
      "Smugglers and signal thieves. Fast frames, dirty electronics." },
    { "BLACK BOX",        FKIND_ROGUE_AI, { 200, 60, 255, 255 },
      "Machines that lost their operators and kept fighting. Reprogrammable." },
};

// ============ MANUFACTURERS ============
// One showroom per zone: ATLAS in Alpha, KESTREL in Beta, HELIOS in Gamma.
const Company companies[NUM_COMPANIES] = {
    { "ATLAS HEAVY INDUSTRIES", "Frontline frames, built to take the hit.", { 255, 170, 90, 255 } },
    { "KESTREL DYNAMICS",       "Recon and electronic warfare, light and fast.", { 120, 230, 255, 255 } },
    { "HELIOS ORDNANCE",        "Long-range artillery platforms.", { 255, 120, 120, 255 } },
};

const CatalogEntry catalog[NUM_CATALOG] = {
    //  company      chassis        level price
    { CO_ATLAS,   MODEL_NOVA,     1,   350 },
    { CO_ATLAS,   MODEL_BULWARK,  1,   450 },
    { CO_ATLAS,   MODEL_RAZOR,    1,   420 },
    { CO_KESTREL, MODEL_WISP,     3,   650 },
    { CO_KESTREL, MODEL_HOUND,    3,   600 },
    { CO_KESTREL, MODEL_STATIC,   3,   700 },
    { CO_HELIOS,  MODEL_LONGBOW,  5,  1150 },
    { CO_HELIOS,  MODEL_HAVOC,    5,  1250 },
};

// ============ JOB BOARD ============
// Posted on the terminal of their zone. Bounties name their target encounter,
// so the board survives the encounter list being reordered.
const JobDef jobDefs[NUM_JOBS] = {
    // ---- SECTOR ALPHA ----
    { "PEST CONTROL", "Alpha Relay Co-op", JOB_CULL, ZONE_ALPHA, NULL, -1, 3,
      120, REWARD_WEAPON, W_GRENADE_LAUNCHER,
      "Feral machines keep chewing through our relay cables. Scrap three in the Alpha ruins." },
    { "FIELD RECOVERY", "Kestrel Dynamics", JOB_RECOVER, ZONE_ALPHA, NULL, -1, 1,
      100, REWARD_CHIP, CHIP_EMERGENCY_EVASION,
      "Reprogram any rogue machine in Sector Alpha. Keep the unit - we only want its telemetry." },
    { "RECRUITER", "Sector Watch", JOB_BOUNTY, ZONE_ALPHA, "PILOT RHEA", -1, 1,
      150, REWARD_MODULE, REFIT_TARGETING_ARRAY,
      "Iron Legion pilot Rhea is recruiting in the calibration field. Put her mech down." },
    { "SPEED TRAP", "Sector Watch", JOB_BOUNTY, ZONE_ALPHA, "SCOUT DANE", -1, 1,
      150, REWARD_MODULE, REFIT_JUMP_JETS,
      "Scout Dane runs Legion messages between sectors. Intercept him." },
    // ---- SECTOR BETA ----
    { "IRON FIST", "Sector Watch", JOB_BOUNTY, ZONE_BETA, "COMMANDER VOLK", -1, 1,
      350, REWARD_MODULE, REFIT_HEAVY_PLATING,
      "Commander Volk holds the industrial ruins for the Legion. Break his line." },
    { "SIGNAL THIEF", "Kestrel Dynamics", JOB_BOUNTY, ZONE_BETA, "ENGINEER KESS", -1, 1,
      350, REWARD_CHIP, CHIP_THERMAL_OPTIMIZATION,
      "Syndicate engineer Kess stole our jamming firmware. Shut her workshop down." },
    { "JAMMER SPECIMEN", "Kestrel Dynamics", JOB_RECOVER, ZONE_BETA, NULL, ARCH_JAMMER, 1,
      250, REWARD_WEAPON, W_VIRUS_UPLINK,
      "A rogue JAMMER is loose in Beta. Reprogram it intact and we'll share the payload we pull from it." },
    { "SCRAP QUOTA", "Atlas Heavy Industries", JOB_CULL, ZONE_BETA, NULL, -1, 4,
      250, REWARD_MECH, MODEL_BULWARK,
      "Clear four rogue machines from our Beta supply route. Payment includes a demo BULWARK." },
    // ---- SECTOR GAMMA ----
    { "WARDEN'S END", "Sector Watch", JOB_BOUNTY, ZONE_GAMMA, "WARDEN KRUX", -1, 1,
      700, REWARD_CHIP, CHIP_OVERCHARGE,
      "Warden Krux commands the Legion from the wasteland. End it." },
    { "GHOST HUNT", "Helios Ordnance", JOB_BOUNTY, ZONE_GAMMA, "GHOST ECHO", -1, 1,
      700, REWARD_MODULE, REFIT_HOVER_SYSTEM,
      "Ghost Echo has been raiding our test ranges. Find the Syndicate ace and scrap the squad." },
    { "ARTILLERY SURVEY", "Helios Ordnance", JOB_RECOVER, ZONE_GAMMA, NULL, ARCH_BOMBARD, 1,
      500, REWARD_WEAPON, W_SIEGE_MORTAR,
      "Reprogram a rogue BOMBARD in Gamma. We want to see what the wasteland did to our design." },
    { "BLACK BOX", "Sector Watch", JOB_BOUNTY, ZONE_GAMMA, "FACTORY OVERSEER", -1, 1,
      1500, REWARD_CHIP, CHIP_DEAD_MAN,
      "The Factory Overseer is building an army out of Gamma scrap. Destroy the core." },
};
