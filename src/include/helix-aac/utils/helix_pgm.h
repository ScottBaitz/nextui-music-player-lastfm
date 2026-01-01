/**
 * PROGMEM compatibility layer for non-Arduino platforms
 * On desktop/embedded Linux, PROGMEM is not used - data stays in RAM
 */
#ifndef HELIX_PGM_H
#define HELIX_PGM_H

#include <string.h>

/* No PROGMEM on Linux - these are no-ops */
#ifndef PROGMEM
#define PROGMEM
#endif

#ifndef pgm_read_byte
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#endif

#ifndef pgm_read_word
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#endif

#ifndef pgm_read_dword
#define pgm_read_dword(addr) (*(const unsigned int *)(addr))
#endif

#ifndef pgm_read_ptr
#define pgm_read_ptr(addr) (*(const void **)(addr))
#endif

#ifndef memcpy_P
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
#endif

#ifndef strcpy_P
#define strcpy_P(dest, src) strcpy((dest), (src))
#endif

#endif /* HELIX_PGM_H */
