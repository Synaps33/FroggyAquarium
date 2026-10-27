TARGET_NAME := aquarium
SRC        := $(wildcard src/*.cpp)
LIBRETRO   := libretro/aquarium_libretro.cpp
RUNNER     := runner/libretro_runner.cpp
HOSTRUNNER := runner/host_runner.cpp
ZLIB_DIR   := third_party/zlib
ZLIB_SRC   := $(ZLIB_DIR)/adler32.c $(ZLIB_DIR)/crc32.c $(ZLIB_DIR)/inflate.c \
              $(ZLIB_DIR)/inffast.c $(ZLIB_DIR)/inftrees.c $(ZLIB_DIR)/zutil.c

MIPS_MUSL := /home/Sajnaps/toolchains/mipsel-linux-muslsf-cross/bin/mipsel-linux-muslsf-
MIPS_ELF  := /opt/frog-toolchain/mipsel-mti-elf_stable/bin/mipsel-mti-elf-
QEMU      := /home/Sajnaps/toolchains/qemu/usr/bin/qemu-mipsel
SYSROOT   := /home/Sajnaps/toolchains/mipsel-linux-muslsf-cross/mipsel-linux-muslsf
MULTICORE ?= /home/Sajnaps/gb300/sf2000_multicore

ifeq ($(platform),sf2000)
PLATFORM := sf2000
endif

COMMON_DEFS := -Dlinux -DSCR_PORT

ifeq ($(PLATFORM),sf2000)
  CC := $(MIPS_ELF)gcc
  CXX := $(MIPS_ELF)g++
  AR := $(MIPS_ELF)ar
  BUILD := build_sf2000
  TARGET := $(TARGET_NAME)_libretro_sf2000.a
  MIPS_FLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -G0 \
                -mno-abicalls -fno-pic -ffreestanding -fno-strict-aliasing \
                -fno-exceptions -fno-rtti -fno-threadsafe-statics \
                -ffunction-sections -fdata-sections -fomit-frame-pointer \
                -fno-unwind-tables -fno-asynchronous-unwind-tables \
                -DSF2000 -DGB300 $(COMMON_DEFS)
  CXXFLAGS := $(MIPS_FLAGS) -std=gnu++17 -Iinclude -I$(ZLIB_DIR) -Isrc -Ilibretro
  CFLAGS   := $(MIPS_FLAGS) -I$(ZLIB_DIR)
  OBJS := $(patsubst src/%.cpp,$(BUILD)/src_%.o,$(SRC)) \
          $(BUILD)/libretro_aquarium_libretro.o \
          $(patsubst $(ZLIB_DIR)/%.c,$(BUILD)/zlib_%.o,$(ZLIB_SRC))
else ifeq ($(PLATFORM),mips)
  CXX := $(MIPS_MUSL)g++
  CC := $(MIPS_MUSL)gcc
  BUILD := build_mips
  TARGET := runner/libretro_runner_mips
  CXXFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -g -static -no-pie \
              -fpermissive -Wno-write-strings -Wno-unused-parameter -Wno-unused-variable \
              -Wno-sign-compare $(COMMON_DEFS) -Isrc -Ilibretro -I$(ZLIB_DIR)
  CFLAGS := -EL -march=mips32 -mtune=mips32r2 -msoft-float -O2 -static -fPIC \
            -I$(ZLIB_DIR) $(COMMON_DEFS)
  ZLIB_OBJS := $(patsubst $(ZLIB_DIR)/%.c,$(BUILD)/zlib_%.o,$(ZLIB_SRC))
  LDLIBS := $(BUILD)/libz.a -lm
else
  CXX := /usr/bin/g++
  BUILD := build_host
  TARGET := build_host/aquarium_runner
  CXXFLAGS := -O2 -g -std=c++17 -fpermissive -Wno-write-strings \
              -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare \
              $(COMMON_DEFS) -Isrc -Ilibretro
  LDLIBS := -lm -lz
endif

ifeq ($(PLATFORM),sf2000)
all: $(TARGET)
$(TARGET): $(SRC) $(LIBRETRO) $(wildcard src/*.h) libretro/libretro.h $(ZLIB_SRC)
	@mkdir -p $(BUILD)
	@set -e; for f in $(SRC); do \
		o=$(BUILD)/src_$$(basename $$f .cpp).o; \
		echo "CXX $$f"; \
		$(CXX) $(CXXFLAGS) -c $$f -o $$o; \
	done
	@set -e; for f in $(ZLIB_SRC); do \
		o=$(BUILD)/zlib_$$(basename $$f .c).o; \
		echo "CC $$f"; \
		$(CC) $(CFLAGS) -c $$f -o $$o; \
	done
	$(CXX) $(CXXFLAGS) -c $(LIBRETRO) -o $(BUILD)/libretro_aquarium_libretro.o
	$(AR) rcs $@ $(OBJS)
	@echo "Built MIPS core archive: $@"

else ifeq ($(PLATFORM),mips)
all: mips-runner
mips-runner: $(TARGET)
$(TARGET): $(SRC) $(LIBRETRO) $(RUNNER) $(ZLIB_SRC) $(wildcard src/*.h) libretro/libretro.h
	@mkdir -p $(BUILD) runner
	@set -e; for f in $(ZLIB_SRC); do \
		o=$(BUILD)/zlib_$$(basename $$f .c).o; \
		$(CC) $(CFLAGS) -c $$f -o $$o; \
	done
	$(MIPS_MUSL)ar rcs $(BUILD)/libz.a $(ZLIB_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LIBRETRO) $(RUNNER) $(LDLIBS)

else
all: host host-runner
host: $(TARGET)
$(TARGET): $(SRC) $(HOSTRUNNER) $(wildcard src/*.h) libretro/libretro.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(HOSTRUNNER) $(LDLIBS)
	@echo "Built $@"

host-runner: build_host/libretro_runner
build_host/libretro_runner: $(SRC) $(LIBRETRO) $(RUNNER) $(wildcard src/*.h) libretro/libretro.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LIBRETRO) $(RUNNER) $(LDLIBS)
	@echo "Built $@"
endif

test: all
	@mkdir -p screenshots/host screenshots/libretro
	AQUARIUM_SEED=12345 ./build_host/aquarium_runner --data assets --config assets/aquarium_test.ini \
		--out screenshots/host/sw --frames 1,30,60,120
	AQUARIUM_SEED=12345 ./build_host/libretro_runner assets/Aquarium.gba screenshots/libretro/lr
	@for f in screenshots/host/*.ppm screenshots/libretro/*.ppm; do \
		ffmpeg -loglevel error -y -i "$$f" "$${f%.ppm}.png"; \
	done

test-mips:
	$(MAKE) PLATFORM=mips mips-runner
	@mkdir -p screenshots/mips screenshots/mips_lr
	AQUARIUM_SEED=12345 $(QEMU) -L $(SYSROOT) ./runner/libretro_runner_mips assets/Aquarium.gba screenshots/mips_lr/lr
	@for f in screenshots/mips_lr/*.ppm; do ffmpeg -loglevel error -y -i "$$f" "$${f%.ppm}.png"; done

device:
	@echo "== building $(TARGET_NAME) for $(FROGGY_TYPE) =="
	$(MAKE) -C $(MULTICORE) CORE=$(CURDIR) CONSOLE=$(TARGET_NAME) FROGGY_TYPE=$(FROGGY_TYPE)
	@mkdir -p dist
	@EXT=sf2k; [ "$(FROGGY_TYPE)" = "GB300V2" ] && EXT=gb3k; \
	cp -f $(MULTICORE)/build/core_87000000 dist/$(TARGET_NAME)_$(FROGGY_TYPE).$$EXT; \
	ls -lh dist/$(TARGET_NAME)_$(FROGGY_TYPE).$$EXT

clean:
	rm -rf build_host build_mips build_sf2000 runner/libretro_runner_mips \
	       $(TARGET_NAME)_libretro_sf2000.a

.PHONY: all host host-runner mips-runner test test-mips device clean
