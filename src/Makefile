###########################################################
# Supported platforms
SUPPORTED_PLATFORMS = tg5040 tg5050
DEFAULT_PLATFORM = tg5040

# Set platform from environment or default
ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
PLATFORM=$(DEFAULT_PLATFORM)
endif

# Validate platform
ifeq (,$(filter $(PLATFORM),$(SUPPORTED_PLATFORMS)))
$(error Invalid PLATFORM '$(PLATFORM)'. Supported: $(SUPPORTED_PLATFORMS))
endif

# Validate cross-compiler
ifeq (,$(CROSS_COMPILE))
$(error Missing CROSS_COMPILE for this toolchain)
endif

###########################################################

TARGET = musicplayer
# Paths adjusted for location in workspace/nextui-music-player/src/
INCDIR = -I. -I./audio -I../../all/common/ -I../../$(PLATFORM)/platform/ -I../../all/minarch/libretro-common/include
INCDIR += -I./include -I./include/mbedtls_lib -I./include/helix-aac

# mbedTLS source files (for HTTPS support)
MBEDTLS_SRC = $(wildcard include/mbedtls_lib/*.c)

# Helix AAC decoder source files
HELIX_AAC_SRC = $(wildcard include/helix-aac/*.c)

SOURCE = $(TARGET).c player.c playlist.c radio.c radio_net.c radio_album_art.c radio_hls.c radio_curated.c youtube.c selfupdate.c \
         ui_fonts.c ui_icons.c ui_utils.c browser.c ui_album_art.c ui_main.c ui_music.c ui_radio.c ui_youtube.c ui_system.c \
         spectrum.c audio/kiss_fft.c audio/kiss_fftr.c \
         include/parson/parson.c \
         include/mbedtls_entropy_alt.c \
         $(MBEDTLS_SRC) \
         $(HELIX_AAC_SRC) \
         ../../all/common/utils.c ../../all/common/api.c ../../all/common/config.c ../../all/common/scaler.c \
         ../../$(PLATFORM)/platform/platform.c

CC = $(CROSS_COMPILE)gcc

# Reset CFLAGS to avoid issues with platform makefile.env
MY_CFLAGS = -O2 -fomit-frame-pointer
# mbedTLS config
MY_CFLAGS += -DMBEDTLS_CONFIG_FILE='<mbedtls_config.h>'
MY_CFLAGS += $(INCDIR) -DPLATFORM=\"$(PLATFORM)\" -std=gnu99
MY_CFLAGS += -DUSE_SDL2 -DUSE_GLES -DGL_GLEXT_PROTOTYPES
MY_CFLAGS += -I$(PREFIX)/include -I$(PREFIX_LOCAL)/include
MY_CFLAGS += -I../../$(PLATFORM)/libmsettings

MY_LDFLAGS = -L$(PREFIX)/lib -L$(PREFIX_LOCAL)/lib
MY_LDFLAGS += -L$(PREFIX)/lib/$(CROSS_TRIPLE)
MY_LDFLAGS += -L../../$(PLATFORM)/libmsettings
MY_LDFLAGS += -L../../all/minarch/build/$(PLATFORM)
MY_LDFLAGS += $(shell pkg-config --libs sdl2 glesv2 2>/dev/null || echo "-lSDL2 -lGLESv2")
MY_LDFLAGS += -lSDL2_image -lSDL2_ttf
MY_LDFLAGS += -lmsettings -lsamplerate -lzip -lm -lpthread -ldl -lz
MY_LDFLAGS += -lasound

# Platform-specific dependencies
ifeq ($(PLATFORM), tg5050)
MY_LDFLAGS += -ltinyalsa
endif

PRODUCT= ../bin/$(PLATFORM)/$(TARGET).elf

all:
	@mkdir -p ../bin/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(MY_CFLAGS) $(MY_LDFLAGS)

clean:
	rm -f ../bin/tg5040/$(TARGET).elf ../bin/tg5050/$(TARGET).elf
