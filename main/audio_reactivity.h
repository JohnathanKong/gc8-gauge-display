#pragma once

// The ESP32-S3 samples the onboard ES7210 microphone only while the
// navigation display is idle. Expose a smoothed normalized envelope,
// never raw PCM, recorded audio, or speech content.
void audio_reactivity_start();
void audio_reactivity_enable(bool enabled);
float audio_reactivity_level();
