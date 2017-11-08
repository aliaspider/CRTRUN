TARGET := CTRRUN

DEBUG                   = 0
BUILD_3DSX              = 1
BUILD_3DS               = 0
BUILD_CIA               = 1
LIBCTRU_NO_DEPRECATION  = 0
CUSTOM_CRT0		= 0

APP_TITLE            = CTRRUN
APP_DESCRIPTION      = CTRRUN
APP_AUTHOR           = various
APP_PRODUCT_CODE     = CTRRUN
ifeq ($(DEBUG), 1)
   APP_UNIQUE_ID        = 0xBC001
else
   APP_UNIQUE_ID        = 0xBC000
endif
APP_ICON             = ctr/icon.png
APP_BANNER           = ctr/banner.png
APP_AUDIO            = ctr/silent.wav
APP_RSF              = ctr/tools/template.rsf
APP_SYSTEM_MODE      = 64MB
APP_SYSTEM_MODE_EXT  = 124MB
APP_BIG_TEXT_SECTION = 0

OBJ :=
OBJ += main.o
OBJ += remote_install.o
OBJ += installurl.o
OBJ += util.o
OBJ += error.o
OBJ += netloader.o

ifeq ($(APP_BIG_TEXT_SECTION), 1)
	LDFLAGS  += -Wl,--defsym,__ctr_patch_services=__service_ptr
endif

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>devkitpro")
endif

APP_TITLE         := $(shell echo "$(APP_TITLE)" | cut -c1-128)
APP_DESCRIPTION   := $(shell echo "$(APP_DESCRIPTION)" | cut -c1-256)
APP_AUTHOR        := $(shell echo "$(APP_AUTHOR)" | cut -c1-128)
APP_PRODUCT_CODE  := $(shell echo $(APP_PRODUCT_CODE) | cut -c1-16)
APP_UNIQUE_ID     := $(shell echo $(APP_UNIQUE_ID) | cut -c1-7)

MAKEROM_ARGS_COMMON = -rsf $(APP_RSF) -exefslogo -elf $(TARGET).elf -icon $(TARGET).icn -banner $(TARGET).bnr -DAPP_TITLE="$(APP_TITLE)" -DAPP_PRODUCT_CODE="$(APP_PRODUCT_CODE)" -DAPP_UNIQUE_ID=$(APP_UNIQUE_ID) -DAPP_SYSTEM_MODE=$(APP_SYSTEM_MODE) -DAPP_SYSTEM_MODE_EXT=$(APP_SYSTEM_MODE_EXT)

INCDIRS := -I$(DEVKITPRO)/libctru/include -I$(DEVKITPRO)/portlibs/armv6k/include
LIBDIRS := -L. -L$(DEVKITPRO)/libctru/lib -L $(DEVKITPRO)/portlibs/armv6k/lib

ARCH  := -march=armv6k -mtune=mpcore -mfloat-abi=hard -marm -mfpu=vfp -mtp=soft

CFLAGS += -mword-relocations -ffast-math -Werror=implicit-function-declaration $(ARCH)

CFLAGS += -Wall
CFLAGS += -DARM11 -D_3DS

ifeq ($(DEBUG), 1)
   CFLAGS   += -O0 -g
   LIBS     += -lctrud
else
   CFLAGS   += -O3 -fomit-frame-pointer
   LIBS     += -lctru
endif

ifeq ($(LIBCTRU_NO_DEPRECATION), 1)
   CFLAGS	+= -DLIBCTRU_NO_DEPRECATION
endif

CFLAGS += -I.

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS	:=  -g $(ARCH) -O3
ifeq ($(CUSTOM_CRT0),1)
   LDFLAGS += -specs=ctr/3dsx_custom.specs
else
   LDFLAGS += -specs=3dsx.specs
endif

LDFLAGS += -g $(ARCH) -Wl,-Map,$(notdir $*.map)

CFLAGS   += -std=gnu99 -ffast-math

LIBS += -lm -lz

ifeq ($(BUILD_3DSX), 1)
TARGET_3DSX := $(TARGET).3dsx $(TARGET).smdh
endif

ifeq ($(BUILD_3DS), 1)
TARGET_3DS := $(TARGET).3ds
endif

ifeq ($(BUILD_CIA), 1)
TARGET_CIA := $(TARGET).cia
endif

.PHONY: $(BUILD) clean all

all: $(TARGET)

$(TARGET): $(TARGET_3DSX) $(TARGET_3DS) $(TARGET_CIA)
$(TARGET).3dsx: $(TARGET).elf
$(TARGET).elf: $(OBJ) libretro_ctr.a

PREFIX := $(DEVKITPRO)/devkitARM/bin/arm-none-eabi-

CC      := $(PREFIX)gcc
CXX     := $(PREFIX)g++
AS      := $(PREFIX)as
AR      := $(PREFIX)ar
OBJCOPY := $(PREFIX)objcopy
STRIP   := $(PREFIX)strip
NM      := $(PREFIX)nm
LD      := $(CXX)

ifneq ($(findstring Linux,$(shell uname -a)),)
	MAKEROM    = ctr/tools/makerom-linux
	BANNERTOOL = ctr/tools/bannertool-linux
else ifneq ($(findstring Darwin,$(shell uname -a)),)
	MAKEROM    = ctr/tools/makerom-mac
	BANNERTOOL = ctr/tools/bannertool-mac
else
	MAKEROM    = ctr/tools/makerom.exe
	BANNERTOOL = ctr/tools/bannertool.exe
endif

%.o: %.vsh %.gsh
	$(DEVKITPRO)/devkitARM/bin/picasso $^ -o $*.shbin
	$(DEVKITPRO)/devkitARM/bin/bin2s $*.shbin | $(PREFIX)as -o $@
	rm $*.shbin

%.o: %.vsh
	$(DEVKITPRO)/devkitARM/bin/picasso $^ -o $*.shbin
	$(DEVKITPRO)/devkitARM/bin/bin2s $*.shbin | $(PREFIX)as -o $@
	rm $*.shbin

%.o: %.cpp
	$(CXX) -c -o $@ $< $(CXXFLAGS) $(INCDIRS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS) $(INCDIRS)

%.o: %.s
	$(CC) -c -o $@ $< $(ASFLAGS)

%.o: %.S
	$(CC) -c -o $@ $< $(ASFLAGS)

%.a:
	$(AR) -rc $@ $^

%.vsh:

$(TARGET).smdh: $(APP_ICON)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" $(APP_ICON) $@

$(TARGET).3dsx: $(TARGET).elf
ifeq ($(APP_BIG_TEXT_SECTION), 1)
	cp ctr/big_text_section.xml $(TARGET).xml
else
	rm -f $(TARGET).xml
endif
	3dsxtool $< $@ $(_3DSXFLAGS)

ifeq ($(CUSTOM_CRT0),1)
$(TARGET).elf: ctr/3dsx_custom_crt0.o
else
$(TARGET).elf:
endif
	$(LD) $(LDFLAGS) $(OBJ) $(LIBDIRS) $(LIBS) -o $@
	$(NM) -CSn $@ > $(notdir $*.lst)

$(TARGET).bnr: $(APP_BANNER) $(APP_AUDIO)
	$(BANNERTOOL) makebanner -i "$(APP_BANNER)" -a "$(APP_AUDIO)" -o $@

$(TARGET).icn: $(APP_ICON)
	$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_TITLE)" -p "$(APP_AUTHOR)" -i $(APP_ICON) -o $@

$(TARGET).3ds: $(TARGET).elf $(TARGET).bnr $(TARGET).icn $(APP_RSF)
	$(MAKEROM) -f cci -o $@ $(MAKEROM_ARGS_COMMON) -DAPP_ENCRYPTED=true

$(TARGET).cia: $(TARGET).elf $(TARGET).bnr $(TARGET).icn $(APP_RSF)
	$(MAKEROM) -f cia -o $@ $(MAKEROM_ARGS_COMMON) -DAPP_ENCRYPTED=false

clean:
	rm -f $(OBJ)
	rm -f $(TARGET).3dsx
	rm -f $(TARGET).elf
	rm -f $(TARGET).3ds
	rm -f $(TARGET).cia
	rm -f $(TARGET).smdh
	rm -f $(TARGET).bnr
	rm -f $(TARGET).icn
	rm -f ctr/ctr_config_*.o
	rm -f ctr/3dsx_custom_crt0.o

.PHONY: clean


