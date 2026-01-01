#include "visualizer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "defines.h"
#include "api.h"

// Simple DFT for frequency analysis (no external FFT library needed)
// This is computationally expensive but works for small sample sizes

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static VisualizerContext vis = {0};

// Simple magnitude calculation for a frequency bin
static float calculate_magnitude(const int16_t* samples, int num_samples, int freq_bin, int sample_rate) {
    float real = 0.0f;
    float imag = 0.0f;
    float freq = (float)freq_bin * sample_rate / num_samples;

    for (int i = 0; i < num_samples; i++) {
        float sample = samples[i] / 32768.0f;
        float angle = 2.0f * M_PI * freq_bin * i / num_samples;
        real += sample * cosf(angle);
        imag += sample * sinf(angle);
    }

    return sqrtf(real * real + imag * imag) / num_samples;
}

// Apply a simple Hann window to reduce spectral leakage
static void apply_window(int16_t* samples, int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        float window = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (num_samples - 1)));
        samples[i] = (int16_t)(samples[i] * window);
    }
}

void Visualizer_init(void) {
    memset(&vis, 0, sizeof(VisualizerContext));

    vis.type = VIS_TYPE_BARS;
    vis.smoothing = 0.7f;

    // Default colors (can be themed)
    vis.bar_color = SDL_MapRGB(SDL_AllocFormat(SDL_PIXELFORMAT_RGB565), 0x00, 0xCC, 0xFF);
    vis.peak_color = SDL_MapRGB(SDL_AllocFormat(SDL_PIXELFORMAT_RGB565), 0xFF, 0xFF, 0xFF);
    vis.wave_color = SDL_MapRGB(SDL_AllocFormat(SDL_PIXELFORMAT_RGB565), 0x00, 0xFF, 0x88);
    vis.bg_color = SDL_MapRGB(SDL_AllocFormat(SDL_PIXELFORMAT_RGB565), 0x10, 0x10, 0x10);
}

void Visualizer_quit(void) {
    memset(&vis, 0, sizeof(VisualizerContext));
}

void Visualizer_setType(VisualizerType type) {
    if (type < VIS_TYPE_COUNT) {
        vis.type = type;
    }
}

VisualizerType Visualizer_getType(void) {
    return vis.type;
}

void Visualizer_nextType(void) {
    vis.type = (vis.type + 1) % VIS_TYPE_COUNT;
}

void Visualizer_processAudio(const int16_t* samples, int num_samples) {
    if (!samples || num_samples <= 0) return;

    // Store waveform for waveform visualization
    int copy_size = num_samples;
    if (copy_size > VIS_FFT_SIZE) copy_size = VIS_FFT_SIZE;
    memcpy(vis.waveform, samples, copy_size * sizeof(int16_t));
    vis.waveform_size = copy_size;

    // For bar visualization, calculate spectrum
    if (vis.type == VIS_TYPE_BARS) {
        // Use only first 512-1024 samples for FFT
        int fft_samples = num_samples;
        if (fft_samples > VIS_FFT_SIZE) fft_samples = VIS_FFT_SIZE;

        // Copy and window the samples
        int16_t windowed[VIS_FFT_SIZE];
        memcpy(windowed, samples, fft_samples * sizeof(int16_t));
        apply_window(windowed, fft_samples);

        // Calculate frequency bands
        // Map logarithmically spaced frequency bins to bars
        int sample_rate = 48000;
        float min_freq = 60.0f;
        float max_freq = 16000.0f;

        for (int bar = 0; bar < VIS_NUM_BARS; bar++) {
            // Logarithmic frequency mapping
            float freq_ratio = (float)bar / VIS_NUM_BARS;
            float freq = min_freq * powf(max_freq / min_freq, freq_ratio);

            // Find the corresponding FFT bin
            int bin = (int)(freq * fft_samples / sample_rate);
            if (bin >= fft_samples / 2) bin = fft_samples / 2 - 1;

            // Calculate magnitude for this bin and nearby bins
            float mag = 0.0f;
            int bin_range = MAX(1, bin / 8);  // Wider bins at higher frequencies

            for (int b = bin; b < bin + bin_range && b < fft_samples / 2; b++) {
                mag += calculate_magnitude(windowed, fft_samples, b, sample_rate);
            }
            mag /= bin_range;

            // Scale the magnitude (log scale looks better)
            float db = 20.0f * log10f(mag + 0.0001f);
            float normalized = (db + 60.0f) / 60.0f;  // -60dB to 0dB range
            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;

            // Apply smoothing
            vis.spectrum[bar] = vis.spectrum[bar] * vis.smoothing +
                               normalized * (1.0f - vis.smoothing);

            // Update peaks
            if (vis.spectrum[bar] > vis.peak[bar]) {
                vis.peak[bar] = vis.spectrum[bar];
                vis.peak_decay[bar] = 0.0f;
            } else {
                vis.peak_decay[bar] += 0.02f;
                vis.peak[bar] -= vis.peak_decay[bar] * 0.05f;
                if (vis.peak[bar] < vis.spectrum[bar]) {
                    vis.peak[bar] = vis.spectrum[bar];
                }
            }
        }
    }
}

static void render_bars(SDL_Surface* dst, SDL_Rect* rect) {
    int bar_width = (rect->w - VIS_NUM_BARS - 1) / VIS_NUM_BARS;
    int spacing = 1;
    int max_height = rect->h - 4;

    // Draw background
    SDL_FillRect(dst, rect, vis.bg_color);

    for (int bar = 0; bar < VIS_NUM_BARS; bar++) {
        int x = rect->x + bar * (bar_width + spacing) + 1;
        int height = (int)(vis.spectrum[bar] * max_height);
        int y = rect->y + rect->h - height - 2;

        // Draw bar
        SDL_Rect bar_rect = {x, y, bar_width, height};
        SDL_FillRect(dst, &bar_rect, vis.bar_color);

        // Draw peak
        int peak_y = rect->y + rect->h - (int)(vis.peak[bar] * max_height) - 4;
        if (peak_y >= rect->y && peak_y < rect->y + rect->h - 2) {
            SDL_Rect peak_rect = {x, peak_y, bar_width, 2};
            SDL_FillRect(dst, &peak_rect, vis.peak_color);
        }
    }
}

static void render_waveform(SDL_Surface* dst, SDL_Rect* rect) {
    // Draw background
    SDL_FillRect(dst, rect, vis.bg_color);

    if (vis.waveform_size <= 0) return;

    int mid_y = rect->y + rect->h / 2;
    int max_amp = rect->h / 2 - 2;

    // Draw waveform as connected points
    int prev_x = rect->x;
    int prev_y = mid_y;

    int step = vis.waveform_size / rect->w;
    if (step < 1) step = 1;

    for (int x = 0; x < rect->w; x++) {
        int sample_idx = (x * vis.waveform_size) / rect->w;
        if (sample_idx >= vis.waveform_size) sample_idx = vis.waveform_size - 1;

        // Average nearby samples for smoothing
        int sum = 0;
        int count = 0;
        for (int i = sample_idx; i < sample_idx + step && i < vis.waveform_size; i++) {
            sum += vis.waveform[i];
            count++;
        }
        int sample = count > 0 ? sum / count : 0;

        int y = mid_y - (sample * max_amp / 32768);
        if (y < rect->y) y = rect->y;
        if (y >= rect->y + rect->h) y = rect->y + rect->h - 1;

        // Draw vertical line from prev_y to y (simple line drawing)
        int x_pos = rect->x + x;
        int y1 = prev_y < y ? prev_y : y;
        int y2 = prev_y > y ? prev_y : y;
        for (int py = y1; py <= y2; py++) {
            SDL_Rect pixel = {x_pos, py, 1, 1};
            SDL_FillRect(dst, &pixel, vis.wave_color);
        }

        prev_y = y;
    }

    // Draw center line
    SDL_Rect center_line = {rect->x, mid_y, rect->w, 1};
    SDL_FillRect(dst, &center_line, SDL_MapRGB(dst->format, 0x40, 0x40, 0x40));
}

void Visualizer_render(SDL_Surface* dst, SDL_Rect* rect) {
    if (!dst || !rect) return;

    switch (vis.type) {
        case VIS_TYPE_BARS:
            render_bars(dst, rect);
            break;
        case VIS_TYPE_WAVE:
            render_waveform(dst, rect);
            break;
        default:
            render_bars(dst, rect);
            break;
    }
}

void Visualizer_setSmoothing(float smoothing) {
    if (smoothing < 0.0f) smoothing = 0.0f;
    if (smoothing > 0.99f) smoothing = 0.99f;
    vis.smoothing = smoothing;
}

void Visualizer_setColors(uint32_t bar, uint32_t peak, uint32_t wave, uint32_t bg) {
    vis.bar_color = bar;
    vis.peak_color = peak;
    vis.wave_color = wave;
    vis.bg_color = bg;
}
