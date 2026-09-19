CXX=g++-6
CXXFLAGS=-std=gnu++14 -O3 -g3 -ggdb -Wall -fmessage-length=0 -m32
LDFLAGS=-m32
LDLIBS=-lpthread -lrt
SRC_DIR = src
OUT_NAME = pup-ipc
TARGET_DIR = bin
TARGET = $(TARGET_DIR)/$(OUT_NAME)
SOURCES = $(shell find $(SRC_DIR) -name "*.cpp" -print)
OBJECTS = $(SOURCES:.cpp=.o)
DEPENDS = $(SOURCES:.cpp=.d)
SRC_SUBDIRS=$(shell find $(SRC_DIR) -type d -print)

.PHONY: clean directories

all:
	mkdir -p $(TARGET_DIR)
	$(MAKE) $(TARGET)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.d: %.cpp
	$(CXX) -M $(CXXFLAGS) $< > $@

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $(TARGET)

clean:
	find . -type f -name '*.o' -delete
	find . -type f -name '*.d' -delete
	rm -rf ./bin

ifneq ($(MAKECMDGOALS), clean)
-include $(DEPENDS)
endif
