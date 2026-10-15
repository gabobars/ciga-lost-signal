#pragma once

#include <stdbool.h>

void ciga_audio_source_register(void);

bool ciga_audio_source_play(
    int version
);

void ciga_audio_source_stop(void);

void ciga_audio_source_unregister(void);

