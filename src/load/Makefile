DANCE_SDK_PLATFORM ?= kontron_type10
DANCE_SDK_DEV_DIR ?= ../../../../components

include /segfs/linux/dance_sdk/build/plain_app.mk

L_FLAGS := -L../../src
C_FLAGS := -Wall -O2 -fPIC -I. -I../../src
C_FILES := main.c
O_FILES := $(C_FILES:.c=.o)

ifeq ($(DANCE_SDK_PLATFORM),conga_imx6)
     C_FLAGS += -DCONFIG_FREESCALE_IMX6=1
endif
ifeq ($(DANCE_SDK_PLATFORM),seco_imx6)
     C_FLAGS += -DCONFIG_FREESCALE_IMX6=1
endif
ifeq ($(DANCE_SDK_PLATFORM),seco_uimx6)
     C_FLAGS += -DCONFIG_FREESCALE_IMX6=1
endif

.PHONY: all install install_local install_sdk clean

all: main

devel: main

main: $(O_FILES)
	$(DANCE_SDK_CC) -static -o $@ $(O_FILES) $(L_FLAGS) $(DANCE_SDK_LFLAGS) $(DANCE_SDK_LIBS)
	$(DANCE_SDK_STRIP) main

%.o: %.c
	$(DANCE_SDK_CC) $(C_FLAGS) $(DANCE_SDK_CFLAGS) -c -o $@ $<

clean:
	-rm $(O_FILES)
	-rm main
