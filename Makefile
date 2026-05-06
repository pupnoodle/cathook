CXX ?= g++
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

LDFLAGS += -shared -z execstack -g -pthread
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
