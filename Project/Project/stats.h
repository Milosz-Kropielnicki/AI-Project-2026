#ifndef STATS_H
#define STATS_H

// ============ ATTRIBUTES ============
// The machine attributes from the design doc (section 3), plus Cooling from the
// Body refit. Stored as an indexed array so every layer (role, model, refit,
// firmware, chips) can be summed, clamped and printed with the same loop.
typedef enum {
    STAT_INTEGRITY,   // 0-200     health pool
    STAT_POWER,       // 0.5-2.0x  damage multiplier
    STAT_ARMOR,       // 0-150     secondary pool on top of Integrity
    STAT_MOBILITY,    // 0-100%    evasion
    STAT_ENERGY,      // 1-5       actions per round
    STAT_HEAT,        // 0-200     heat capacity
    STAT_COOLING,     // heat dissipated per round
    STAT_ACCURACY,    // 0-100%
    STAT_STABILITY,   // 0-100%    resistance to scrambling
    NUM_STATS
} StatId;

typedef struct { float v[NUM_STATS]; } Stats;

const char* statName(StatId s);        // "INTEGRITY"
const char* statShort(StatId s);       // "INT"
const char* statFormat(StatId s, float value);   // "1.15x", "35%", "150" (TextFormat buffer)
float statClamp(StatId s, float value);
void statsAdd(Stats* dst, const Stats* src);

#endif
