/**
 * Configuration file for Helix AAC decoder
 * Minimal config for embedded Linux
 */
#ifndef CONFIG_HELIX_H
#define CONFIG_HELIX_H

/* Enable SBR (Spectral Band Replication) for HE-AAC */
#define HELIX_FEATURE_AUDIO_CODEC_AAC_SBR

/* Use default standard library */
#define USE_DEFAULT_STDLIB

#endif /* CONFIG_HELIX_H */
