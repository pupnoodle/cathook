CXX ?= g++
SHELL := $(if $(wildcard /bin/sh),/bin/sh,$(if $(wildcard /bin/bash),/bin/bash,/usr/bin/bash))
BUILD_MAKEFILE := $(lastword $(MAKEFILE_LIST))

MAKEFLAGS := --jobs=$(shell nproc)

TEXTMODE ?= 0
OUTPUT_DIR ?= bin
INSTALL_PREFIX ?= /opt/cathook
INSTALL_BIN_DIR ?= $(INSTALL_PREFIX)/bin
INSTALL_IPC_DIR ?= $(INSTALL_PREFIX)/ipc
CATBOT_IPC_DIR = botpanel/catbot-ipc-server-main

CPPFLAGS += -I. -Isrc -Isrc/external
CPPFLAGS += -DGIT_COMMIT_HASH=\"$(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)\"
CPPFLAGS += -DGIT_COMMITTER_DATE=\"$(shell git log -1 --format=%cd --date=short 2>/dev/null || date +%Y-%m-%d)\"
CXXFLAGS ?= -std=c++23 -g --no-gnu-unique -pthread -fPIC
DEPFLAGS = -MMD -MP
CATHOOK_DEBUG_SYMBOLS ?= 0

ifeq ($(CATHOOK_DEBUG_SYMBOLS),1)
CXXFLAGS += -g3 -ggdb3 -fno-omit-frame-pointer -fno-optimize-sibling-calls
LDFLAGS += -Wl,--build-id=sha1
endif

ifeq ($(TEXTMODE),1)
CPPFLAGS += -DCATHOOK_TEXTMODE=1
BUILD_MODE = textmode
BIN_NAME = libcathooktextmode.so
else
BUILD_MODE = default
BIN_NAME = libcathook.so
endif

BIN = $(OUTPUT_DIR)/$(BIN_NAME)
OBJ_DIR = obj/$(BUILD_MODE)

LDFLAGS += -shared -Wl,-z,noexecstack -g -pthread
LDFLAGS += -Wl,-rpath,'$$ORIGIN' -Wl,-rpath,/usr/lib -Wl,-rpath,/usr/lib64 -Wl,-rpath,/run/host/usr/lib -Wl,-rpath,/run/host/usr/lib64
LDLIBS += -lGLEW -lGL -lSDL2 -lvulkan libs/funchook/libfunchook.a libs/funchook/libdistorm.a

OBJ_FILES =  src/cathook.cpp.o # Unity build
OBJ_FILES += src/core/logger.cpp.o src/core/config/config_store.cpp.o src/core/diagnostics/exception_handler.cpp.o # Core systems
OBJ_FILES += src/external/MD5/MD5.cpp.o # MD5 helpers
OBJ_FILES += src/external/libsigscan/libsigscan.c.o # Sigscan library
OBJ_FILES += src/external/imgui/imgui_tables.cpp.o src/external/imgui/imgui_draw.cpp.o src/external/imgui/imgui_impl_sdl2.cpp.o src/external/imgui/imgui_demo.cpp.o src/external/imgui/imgui_impl_opengl3.cpp.o src/external/imgui/imgui_impl_vulkan.cpp.o src/external/imgui/imgui_widgets.cpp.o src/external/imgui/imgui.cpp.o src/external/imgui/imgui_stdlib.cpp.o # GUI library and OpenGL wrapper

OBJS = $(addprefix $(OBJ_DIR)/, $(OBJ_FILES))
DEPS = $(OBJS:.o=.d)

.PHONY: all both catbot_ipc clean debug install textmode

#-------------------------------------------------------------------------------

all: $(BIN) catbot_ipc

textmode:
	$(MAKE) TEXTMODE=1

both:
	$(MAKE)
	$(MAKE) TEXTMODE=1

catbot_ipc:
	$(MAKE) -C "$(CATBOT_IPC_DIR)" REPO_ROOT="$(abspath .)"

clean:
	rm -rf obj
	rm -f $(OUTPUT_DIR)/libcathook.so $(OUTPUT_DIR)/libcathooktextmode.so
	rm -f tf2.so tf2_textmode.so
	$(MAKE) -C "$(CATBOT_IPC_DIR)" clean

install: $(BIN) catbot_ipc
	install -d -m 0755 "$(INSTALL_BIN_DIR)" "$(INSTALL_IPC_DIR)/bin"
	install -m 0755 "$(BIN)" "$(INSTALL_BIN_DIR)/$(BIN_NAME)"
	if [ -f "$(OUTPUT_DIR)/libcathooktextmode.so" ]; then install -m 0755 "$(OUTPUT_DIR)/libcathooktextmode.so" "$(INSTALL_BIN_DIR)/libcathook-textmode.so"; fi
	if [ -f "$(OUTPUT_DIR)/libcathook.so" ]; then install -m 0755 "$(OUTPUT_DIR)/libcathook.so" "$(INSTALL_BIN_DIR)/libcathook.so"; fi
	@for binary_path in "$(OUTPUT_DIR)/libcathook.so" "$(OUTPUT_DIR)/libcathooktextmode.so"; do \
		[ -f "$$binary_path" ] || continue; \
		if ! command -v readelf >/dev/null 2>&1; then \
			echo "Warning: readelf is missing; cannot bundle libGLEW fallback for $$binary_path." >&2; \
			continue; \
		fi; \
		required_library="$$(readelf -d "$$binary_path" 2>/dev/null | awk -F'[][]' '/NEEDED/ && $$2 ~ /^libGLEW\.so\./ { print $$2; exit }')"; \
		[ -n "$$required_library" ] || continue; \
		[ ! -f "$(INSTALL_BIN_DIR)/$$required_library" ] || continue; \
		source_path="$$(ldconfig -p 2>/dev/null | awk -v library_name="$$required_library" '$$1 == library_name { print $$NF; exit }')"; \
		for candidate in "/usr/lib/$$required_library" "/usr/lib64/$$required_library" "/usr/lib/x86_64-linux-gnu/$$required_library" "/usr/local/lib/$$required_library" "/run/host/usr/lib/$$required_library" "/run/host/usr/lib64/$$required_library"; do \
			[ -n "$$source_path" ] && [ -f "$$source_path" ] && break; \
			if [ -f "$$candidate" ]; then source_path="$$candidate"; fi; \
		done; \
		if [ -z "$$source_path" ] || [ ! -f "$$source_path" ]; then \
			echo "Warning: $$binary_path needs $$required_library, but it was not found on this system." >&2; \
			continue; \
		fi; \
		install -m 0755 "$$source_path" "$(INSTALL_BIN_DIR)/$$required_library"; \
		echo "Installed bundled fallback $$required_library to $(INSTALL_BIN_DIR)"; \
	done
	$(MAKE) -C "$(CATBOT_IPC_DIR)" REPO_ROOT="$(abspath .)" INSTALL_DIR="$(INSTALL_IPC_DIR)" install

#-------------------------------------------------------------------------------

$(BIN): $(OBJS) $(BUILD_MAKEFILE)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJ_DIR)/%.cpp.o: %.cpp $(BUILD_MAKEFILE)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.c.o: %.c $(BUILD_MAKEFILE)
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(DEPS)
