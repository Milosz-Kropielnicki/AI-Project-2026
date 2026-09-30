#include "audio.h"
#include "mech.h"
#include "raylib.h"
#include <math.h>
#include <string.h>

#define RATE 22050
#define MASTER_VOLUME 0.7f

static Sound sfx[NUM_SFX];
static Sound fire[NUM_MUNITIONS];
static Sound hit[NUM_MUNITIONS];
static int ready = 0, muted = 0;

// ============ SYNTH ============
// A tiny additive/noise synth writing into a float buffer, then quantized to
// 16-bit. Envelopes are exponential decays so everything sounds percussive.
#define MAX_SECONDS 1.2f
static float buf[(int)(RATE * MAX_SECONDS)];
static int len;
static unsigned int seed = 12345;

static float noise(void) {
    seed = seed * 1664525u + 1013904223u;
    return (float)((seed >> 9) & 0x7FFF) / 16384.0f - 1.0f;
}

static void begin(float seconds) {
    len = (int)(RATE * seconds);
    if (len > (int)(RATE * MAX_SECONDS)) len = (int)(RATE * MAX_SECONDS);
    memset(buf, 0, sizeof(float) * len);
}

// Sine / square sweep from f0 to f1 with an exponential decay
static void tone(float f0, float f1, float amp, float decay, int square) {
    float phase = 0;
    for (int i = 0; i < len; i++) {
        float t = (float)i / len;
        float f = f0 + (f1 - f0) * t;
        phase += 2 * PI * f / RATE;
        float v = sinf(phase);
        if (square) v = v > 0 ? 1.0f : -1.0f;
        buf[i] += v * amp * expf(-decay * t);
    }
}

// Noise through a one-pole low-pass (lp near 1 = darker), optional attack
static void hiss(float amp, float decay, float lp, float attack) {
    float y = 0;
    for (int i = 0; i < len; i++) {
        float t = (float)i / len;
        y = y * lp + noise() * (1 - lp);
        float env = expf(-decay * t);
        if (attack > 0 && t < attack) env *= t / attack;
        buf[i] += y * amp * env * (lp > 0.5f ? 3.0f : 1.0f);
    }
}

// Amplitude wobble (bubbles, warbles)
static void wobble(float hz, float depth) {
    for (int i = 0; i < len; i++)
        buf[i] *= 1.0f - depth * (0.5f + 0.5f * sinf(2 * PI * hz * i / RATE));
}

// Sparse clicks (crackle)
static void crackle(float density, float amp) {
    for (int i = 0; i < len; i++)
        if (noise() > 1.0f - density) {
            float t = (float)i / len;
            for (int k = 0; k < 40 && i + k < len; k++) buf[i + k] += noise() * amp * (1 - t) * (1 - k / 40.0f);
        }
}

static Sound finish(void) {
    short* data = (short*)MemAlloc(len * sizeof(short));
    for (int i = 0; i < len; i++) {
        float v = buf[i];
        if (v > 1) v = 1;
        if (v < -1) v = -1;
        data[i] = (short)(v * 32000);
    }
    Wave w = { (unsigned int)len, RATE, 16, 1, data };
    Sound s = LoadSoundFromWave(w);
    UnloadWave(w);
    return s;
}

// ============ VOICES ============
static Sound makeFire(int m) {
    switch (m) {
    case MUN_BALLISTIC:       begin(0.16f); hiss(0.9f, 18, 0.2f, 0); tone(140, 60, 0.6f, 14, 0); break;         // crack + thump
    case MUN_ENERGY:          begin(0.26f); tone(1500, 300, 0.5f, 6, 0); tone(1510, 305, 0.2f, 6, 1); break;     // pew
    case MUN_THERMAL:         begin(0.45f); hiss(0.8f, 3, 0.9f, 0.15f); crackle(0.004f, 0.2f); break;          // roar
    case MUN_ELECTROMAGNETIC: begin(0.34f); tone(240, 180, 0.35f, 4, 1); wobble(55, 0.8f); crackle(0.01f, 0.3f); break;  // buzz
    case MUN_EXPLOSIVE:       begin(0.32f); tone(220, 70, 0.7f, 6, 0); hiss(0.5f, 8, 0.7f, 0); break;           // launch
    default:                  begin(0.36f); hiss(0.6f, 4, 0.1f, 0.05f); wobble(11, 0.7f); break;                // hiss
    }
    return finish();
}

static Sound makeHit(int m) {
    switch (m) {
    case MUN_BALLISTIC:       begin(0.18f); tone(900, 850, 0.3f, 16, 0); tone(1330, 1300, 0.25f, 18, 0); hiss(0.4f, 30, 0.1f, 0); break;  // clank
    case MUN_ENERGY:          begin(0.22f); hiss(0.5f, 10, 0.05f, 0); tone(620, 600, 0.3f, 12, 0); break;       // sizzle
    case MUN_THERMAL:         begin(0.42f); crackle(0.02f, 0.5f); hiss(0.3f, 5, 0.85f, 0); break;               // crackling burn
    case MUN_ELECTROMAGNETIC: begin(0.22f); tone(900, 90, 0.45f, 5, 1); break;                                   // zap
    case MUN_EXPLOSIVE:       begin(0.80f); hiss(1.0f, 5, 0.93f, 0); tone(70, 40, 0.8f, 5, 0); break;           // boom
    default:                  begin(0.30f); hiss(0.6f, 9, 0.5f, 0); wobble(18, 0.6f); break;                    // splat
    }
    return finish();
}

static Sound makeSfx(Sfx s) {
    switch (s) {
    case SFX_UI_MOVE:        begin(0.04f); tone(1200, 1200, 0.25f, 20, 0); break;
    case SFX_UI_CONFIRM:     begin(0.12f); tone(700, 1100, 0.3f, 6, 1); break;
    case SFX_UI_DENY:        begin(0.18f); tone(160, 120, 0.35f, 5, 1); break;
    case SFX_MISS:           begin(0.22f); hiss(0.4f, 3, 0.6f, 0.4f); tone(500, 250, 0.1f, 6, 0); break;      // whoosh
    case SFX_ARMOR_HIT:      begin(0.16f); tone(420, 400, 0.4f, 14, 0); tone(610, 600, 0.3f, 16, 0); break;    // plate ring
    case SFX_INTEGRITY_HIT:  begin(0.20f); hiss(0.8f, 12, 0.4f, 0); tone(90, 50, 0.6f, 10, 0); break;         // crunch
    case SFX_SCRAPPED:       begin(0.9f);  hiss(0.9f, 3, 0.95f, 0); tone(110, 30, 0.6f, 3, 0); crackle(0.01f, 0.4f); break;
    case SFX_HEAT_WARN:      begin(0.30f); tone(880, 880, 0.3f, 1, 1); for (int i = len / 3; i < len * 2 / 3; i++) buf[i] = 0; break;  // beep-beep
    case SFX_SCRAMBLE:       begin(0.40f); tone(300, 1200, 0.3f, 3, 1); wobble(30, 0.9f); break;
    case SFX_HACK:           begin(0.50f); tone(400, 1600, 0.25f, 2, 0); wobble(12, 0.5f); break;
    case SFX_VICTORY:        begin(0.60f); for (int k = 0; k < 3; k++) { float f = 523.0f * powf(1.26f, (float)k);
                                 float phase = 0; for (int i = len * k / 3; i < len; i++) { phase += 2 * PI * f / RATE;
                                 buf[i] += sinf(phase) * 0.2f * expf(-4.0f * (i - len * k / 3) / (float)len); } } break;
    default:                 begin(0.70f); tone(400, 90, 0.4f, 3, 1); break;   // defeat
    }
    return finish();
}

// ============ API ============
void audioInit(void) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    SetMasterVolume(MASTER_VOLUME);
    for (int m = 0; m < NUM_MUNITIONS; m++) { fire[m] = makeFire(m); hit[m] = makeHit(m); }
    for (int s = 0; s < NUM_SFX; s++) sfx[s] = makeSfx((Sfx)s);
    ready = 1;
}

void audioShutdown(void) {
    if (!ready) return;
    for (int m = 0; m < NUM_MUNITIONS; m++) { UnloadSound(fire[m]); UnloadSound(hit[m]); }
    for (int s = 0; s < NUM_SFX; s++) UnloadSound(sfx[s]);
    CloseAudioDevice();
    ready = 0;
}

void audioToggleMute(void) {
    muted = !muted;
    if (ready) SetMasterVolume(muted ? 0 : MASTER_VOLUME);
}

int audioMuted(void) { return muted; }

void sfxPlay(Sfx s) { if (ready) PlaySound(sfx[s]); }
void sfxFire(int munition) { if (ready && munition >= 0 && munition < NUM_MUNITIONS) PlaySound(fire[munition]); }
void sfxImpact(int munition) { if (ready && munition >= 0 && munition < NUM_MUNITIONS) PlaySound(hit[munition]); }
