#ifndef AUDIO_H
#define AUDIO_H

// Procedural sound effects. Everything is synthesized at startup, so the game
// ships no audio files. Each munition type has its own fire and impact voice.

typedef enum {
    SFX_UI_MOVE, SFX_UI_CONFIRM, SFX_UI_DENY,
    SFX_MISS, SFX_ARMOR_HIT, SFX_INTEGRITY_HIT, SFX_SCRAPPED,
    SFX_HEAT_WARN, SFX_SCRAMBLE, SFX_HACK,
    SFX_VICTORY, SFX_DEFEAT,
    NUM_SFX
} Sfx;

void audioInit(void);
void audioShutdown(void);
void audioToggleMute(void);
int audioMuted(void);

void sfxPlay(Sfx s);
void sfxFire(int munition);      // weapon discharge
void sfxImpact(int munition);    // projectile / beam arriving

#endif
