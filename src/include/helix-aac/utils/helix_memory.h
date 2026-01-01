/**
 * Memory allocation helpers for Helix AAC decoder
 * On embedded Linux, we use standard malloc/free
 */
#ifndef HELIX_MEMORY_H
#define HELIX_MEMORY_H

#include <stdlib.h>
#include <string.h>

/* Standard memory allocation - no PSRAM on Linux */
#define helix_malloc(size) malloc(size)
#define helix_calloc(nmemb, size) calloc(nmemb, size)
#define helix_free(ptr) free(ptr)

#endif /* HELIX_MEMORY_H */
