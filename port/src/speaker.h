/*
 * speaker.h - PC speaker emulation: PIT channel 2 square wave gated by port
 * 61h, rendered through an SDL2 audio device.
 *
 * The game thread reports every change of the speaker state with a
 * timestamp from the game clock (game_clock_ns, paced 8088 time); the audio
 * thread replays those changes a fixed latency behind
 * real time, so the sound keeps the timing the game produced.
 */
#ifndef SAB2_SPEAKER_H
#define SAB2_SPEAKER_H

#include <stdbool.h>
#include <stdint.h>

/* Opens the default playback device. Requires SDL_INIT_AUDIO. */
bool speaker_init(void);
void speaker_shutdown(void);

/* Game thread: new port 61h value (bit 0 = PIT channel 2 gate, bit 1 =
 * speaker data) and channel 2 reload count (0 = 65536). */
void speaker_set(uint8_t port61, uint16_t pit2_reload);

#endif
