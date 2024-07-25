TARGET = BO3MacFix.dylib

DEBUG   ?= 0
ARCH    ?= x86_64
SDK     ?= macosx

SYSROOT  := $(shell xcrun --sdk $(SDK) --show-sdk-path)
ifeq ($(SYSROOT),)
$(error Could not find SDK "$(SDK)")
endif
CLANG    := clang
CC       := $(CLANG) -isysroot $(SYSROOT) -arch $(ARCH)

CFLAGS  = -O1 -Wall -g -I include/ -I src/
LDFLAGS = -shared -Wl,-undefined -Wl,dynamic_lookup -Llib -ldobby

FRAMEWORKS = -framework CoreFoundation

SOURCES = source/bo3macfix.c source/bo3macnative.m source/utilities.c

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(FRAMEWORKS) $(DEFINES) $(LDFLAGS) -o $@ $(SOURCES)

clean:
	rm -f -- $(TARGET)
