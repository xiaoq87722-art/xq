.PHONY: all clean install

CXX      ?= g++
AR       ?= ar
CXXFLAGS ?= -std=c++20 -O2 -g -fno-omit-frame-pointer -DNDEBUG -march=native -Wall
INCLUDE  := -I. -I/usr/local/include/mimalloc-3.2

TARGET    := libxq.a
BUILD_DIR := build
OBJ_DIR   := $(BUILD_DIR)/obj
MERGE_DIR := $(BUILD_DIR)/merge

SRCS := $(wildcard src/net/*.cpp) $(wildcard src/utils/*.cpp)
OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

THIRDPARTY_LIBS := /usr/local/lib/libspdlog.a \
                   /usr/local/lib/mimalloc-3.2/libmimalloc.a

PREFIX ?= /usr/local

all: $(TARGET)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -MMD -MP -c -o $@ $<

$(TARGET): $(OBJS) $(THIRDPARTY_LIBS)
	@rm -rf $(MERGE_DIR)
	@set -e; for lib in $(THIRDPARTY_LIBS); do \
		name=$$(basename $$lib .a); \
		dir=$(MERGE_DIR)/$$name; \
		mkdir -p $$dir; \
		(cd $$dir && $(AR) x $$lib); \
	done
	@echo "AR  $@"
	@rm -f $@
	$(AR) rcs $@ $(OBJS) $$(find $(MERGE_DIR) -name '*.o')
	@echo "built $@ (size: $$(du -h $@ | cut -f1))"

install: $(TARGET)
	install -d $(PREFIX)/lib $(PREFIX)/include
	install -m 644 $(TARGET) $(PREFIX)/lib/
	cp -r xq $(PREFIX)/include/

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPS)
