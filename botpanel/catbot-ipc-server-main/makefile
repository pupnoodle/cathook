CXX ?= g++
SHELL := $(if $(wildcard /bin/sh),/bin/sh,$(if $(wildcard /bin/bash),/bin/bash,/usr/bin/bash))
BUILD_MAKEFILE := $(lastword $(MAKEFILE_LIST))
REPO_ROOT ?= ../..
OUT_DIR = bin
INSTALL_DIR ?= /opt/cathook/ipc
TARGETS = server exec undeadlock exec_all console

CXXFLAGS ?= -std=c++23 -O2 -g -Wall -Wextra -fmessage-length=0
CXXFLAGS += -I$(REPO_ROOT)/src -I$(SRC_DIR)
DEPFLAGS = -MMD -MP
LDLIBS = -pthread -lrt
SRC_DIR = src
DEPS = $(addprefix $(OUT_DIR)/,$(addsuffix .d,$(TARGETS)))
IPC_SHARED_HEADERS = \
	$(REPO_ROOT)/src/core/ipc/ipc_protocol.hpp \
	$(REPO_ROOT)/src/core/ipc/ipc_shared.hpp

.PHONY: all clean install

all: $(addprefix $(OUT_DIR)/,$(TARGETS))

install: all
	install -d -m 0755 "$(INSTALL_DIR)/bin"
	install -m 0755 $(addprefix $(OUT_DIR)/,$(TARGETS)) "$(INSTALL_DIR)/bin/"

$(OUT_DIR)/%: $(SRC_DIR)/%.cpp $(BUILD_MAKEFILE) $(IPC_SHARED_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -MF $(OUT_DIR)/$*.d -MT $@ $< $(LDLIBS) -o $@

clean:
	rm -rf "$(OUT_DIR)"

-include $(DEPS)
