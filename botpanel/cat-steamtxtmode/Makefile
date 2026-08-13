CXX ?= g++
ARCH ?= 32

ifeq ($(ARCH),32)
OUTPUT_DIR := bin/lib64
ARCH_FLAGS := -m32
ELF_CHECK := ELF 32-bit
ELF_CLASS := ELF32
else ifeq ($(ARCH),64)
OUTPUT_DIR := bin/libx64
ARCH_FLAGS :=
ELF_CHECK := ELF 64-bit
ELF_CLASS := ELF64
else
$(error ARCH must be 32 or 64)
endif

TARGET := $(OUTPUT_DIR)/libcatsteamtxtmode.so
STANDALONE_TARGET := $(OUTPUT_DIR)/catsteamtxtmode
TEST_TARGET := $(OUTPUT_DIR)/tf_linux64
STANDALONE_SOURCE := src/cat_steamtxtmode_standalone.cpp
SOURCES := $(filter-out $(STANDALONE_SOURCE),$(wildcard src/*.cpp))

CXXFLAGS ?= -O2 -pipe -std=c++23 -fPIC -fvisibility=hidden -Wall -Wextra -Wpedantic
LDFLAGS ?= -shared -Wl,-z,relro,-z,now -Wl,-z,noexecstack
LDLIBS := -ldl -pthread

.PHONY: all standalone clean verify test

all: $(TARGET) $(STANDALONE_TARGET)

$(TARGET): $(SOURCES) Makefile

	@mkdir -p $(dir $@)
	$(CXX) $(ARCH_FLAGS) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SOURCES) $(LDLIBS)

$(STANDALONE_TARGET): $(STANDALONE_SOURCE) Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(ARCH_FLAGS) -O2 -pipe -std=c++23 -Wall -Wextra -Wpedantic -o $@ $(STANDALONE_SOURCE)

standalone: $(STANDALONE_TARGET)

verify: $(TARGET) $(STANDALONE_TARGET)
	file $(TARGET) | grep -q '$(ELF_CHECK)'
	readelf -h $(TARGET) | grep -q 'Class:.*$(ELF_CLASS)'
	nm -D $(TARGET) | grep -q ' dlopen$$'
	$(STANDALONE_TARGET) --help >/dev/null

test: $(TARGET) tests/steam_fingerprint_test.cpp
	$(CXX) $(ARCH_FLAGS) -O2 -std=c++23 -Wall -Wextra -Wpedantic -o $(TEST_TARGET) tests/steam_fingerprint_test.cpp -ldl
	CAT_STEAM_TXTMODE=1 CAT_STEAM_TXTMODE_SYNTHETIC_HARDWARE=1 CAT_BOT_ID=steamtxtmode-test LD_PRELOAD="$(abspath $(TARGET))" $(TEST_TARGET)

clean:
	rm -f bin/lib64/libcatsteamtxtmode.so bin/libx64/libcatsteamtxtmode.so bin/lib64/catsteamtxtmode bin/libx64/catsteamtxtmode bin/lib64/tf_linux64 bin/libx64/tf_linux64
