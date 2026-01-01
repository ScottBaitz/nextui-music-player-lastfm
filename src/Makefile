###########################################################

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

ifeq (,$(PLATFORM))
	$(error please specify PLATFORM, eg. PLATFORM=trimui make)
endif

ifeq (,$(CROSS_COMPILE))
	$(error missing CROSS_COMPILE for this toolchain)
endif

###########################################################

# Note: We don't include makefile.env to avoid CFLAGS conflicts
# include ../../$(PLATFORM)/platform/makefile.env

###########################################################

TARGET = musicplayer
INCDIR = -I. -I./audio -I../common/ -I../../$(PLATFORM)/platform/ -I../minarch/libretro-common/include
INCDIR += -I./include -I./include/mbedtls_lib -I./include/helix-aac

# mbedTLS source files (for HTTPS support)
MBEDTLS_SRC = $(wildcard include/mbedtls_lib/*.c)

# Helix AAC decoder source files
HELIX_AAC_SRC = $(wildcard include/helix-aac/*.c)

SOURCE = $(TARGET).c player.c visualizer.c radio.c youtube.c \
         include/parson/parson.c \
         include/mbedtls_entropy_alt.c \
         $(MBEDTLS_SRC) \
         $(HELIX_AAC_SRC) \
         ../common/utils.c ../common/api.c ../common/config.c ../common/scaler.c \
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
MY_CFLAGS += -I../../$(PLATFORM)/wifimanager/daemon
MY_CFLAGS += -I../../$(PLATFORM)/btmanager/src/include

MY_LDFLAGS = -L$(PREFIX)/lib -L$(PREFIX_LOCAL)/lib
MY_LDFLAGS += -L../../$(PLATFORM)/libmsettings
MY_LDFLAGS += -L../../$(PLATFORM)/wifimanager/src/core
MY_LDFLAGS += -L../../$(PLATFORM)/wifimanager/daemon
MY_LDFLAGS += -L../minarch/build/$(PLATFORM)
MY_LDFLAGS += -L../../$(PLATFORM)/btmanager/src/lib/aarch64/glibc-gcc8_3_0
MY_LDFLAGS += -lSDL2 -lSDL2_image -lSDL2_ttf -lGLESv2 -lEGL
MY_LDFLAGS += -lmsettings -lsamplerate -lm -lpthread -ldl -lz

ifeq ($(PLATFORM), tg5040)
MY_CFLAGS += -DHAS_WIFIMG -DHAS_BTMG
MY_LDFLAGS += -lwifimg -lwifid
MY_LDFLAGS += -lbtmg -lglib-2.0 -lgio-2.0 -lshared-mainloop -lbluetooth-internal -lasound -ljson-c
endif

PRODUCT= build/$(PLATFORM)/$(TARGET).elf

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SOURCE) -o $(PRODUCT) $(MY_CFLAGS) $(MY_LDFLAGS)

clean:
	rm -f $(PRODUCT)
