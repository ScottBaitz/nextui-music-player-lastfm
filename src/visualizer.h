#ifndef __VISUALIZER_H__
#define __VISUALIZER_H__

#include <stdint.h>
#include "sdl.h"

#define VIS_NUM_BARS 32
#define VIS_FFT_SIZE 1024

// Visualizer types
typedef enum {
    VIS_TYPE_BARS = 0,
    VIS_TYPE_WAVE,
    VIS_TYPE_COUNT
} VisualizerType;

// Visualizer context
typedef struct {
    VisualizerType type;

    // FFT data
    float spectrum[VIS_NUM_BARS];
    float peak[VIS_NUM_BARS];
    float peak_decay[VIS_NUM_BARS];

    // Waveform data
    int16_t waveform[VIS_FFT_SIZE];
    int waveform_size;

    // Smoothing
    float smoothing;

    // Colors
    uint32_t bar_color;
    uint32_t peak_color;
    uint32_t wave_color;
    uint32_t bg_color;
} VisualizerContext;

// Initialize the visualizer
void Visualizer_init(void);

// Cleanup
void Visualizer_quit(void);

// Set visualizer type
void Visualizer_setType(VisualizerType type);

// Get current type
VisualizerType Visualizer_getType(void);

// Cycle to next type
void Visualizer_nextType(void);

// Process audio samples (call this with audio data)
void Visualizer_processAudio(const int16_t* samples, int num_samples);

// Render the visualization to a surface
void Visualizer_render(SDL_Surface* dst, SDL_Rect* rect);

// Set smoothing factor (0.0 to 1.0, higher = smoother)
void Visualizer_setSmoothing(float smoothing);

// Set colors
void Visualizer_setColors(uint32_t bar, uint32_t peak, uint32_t wave, uint32_t bg);

#endif
