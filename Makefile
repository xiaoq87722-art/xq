.PHONY: all release debug clean install

CXX ?= g++
AR  ?= ar

COMMON_FLAGS  := -std=c++20 -march=native -Wall
RELEASE_FLAGS := $(COMMON_FLAGS) -O2 -DNDEBUG
DEBUG_FLAGS   := $(COMMON_FLAGS) -O0 -g3 -fno-omit-frame-pointer

INCLUDE := -I. -I/usr/local/include/mimalloc-3.2

TARGET_RELEASE := libxq.a
TARGET_DEBUG   := libxq_debug.a

BUILD_DIR   := build
OBJ_DIR_R   := $(BUILD_DIR)/release
OBJ_DIR_D   := $(BUILD_DIR)/debug
MERGE_DIR_R := $(BUILD_DIR)/merge_release
MERGE_DIR_D := $(BUILD_DIR)/merge_debug

SRCS   := $(wildcard src/net/*.cpp) $(wildcard src/utils/*.cpp)
OBJS_R := $(patsubst %.cpp,$(OBJ_DIR_R)/%.o,$(SRCS))
OBJS_D := $(patsubst %.cpp,$(OBJ_DIR_D)/%.o,$(SRCS))
DEPS   := $(OBJS_R:.o=.d) $(OBJS_D:.o=.d)

THIRDPARTY_LIBS := /usr/local/lib/libspdlog.a \
                   /usr/local/lib/mimalloc-3.2/libmimalloc.a

PREFIX ?= /usr/local

all: release

release:
	@START_TIME=$$(date +%s); \
	$(MAKE) $(TARGET_RELEASE) --no-print-directory; \
	END_TIME=$$(date +%s); \
	ELAPSED=$$(($$END_TIME - $$START_TIME)); \
	echo "Build release took $$ELAPSED seconds."

debug:
	@START_TIME=$$(date +%s); \
	$(MAKE) $(TARGET_DEBUG) --no-print-directory; \
	END_TIME=$$(date +%s); \
	ELAPSED=$$(($$END_TIME - $$START_TIME)); \
	echo "Build debug took $$ELAPSED seconds."

$(OBJ_DIR_R)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(RELEASE_FLAGS) $(INCLUDE) -MMD -MP -c -o $@ $<

$(OBJ_DIR_D)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(DEBUG_FLAGS) $(INCLUDE) -MMD -MP -c -o $@ $<

$(TARGET_RELEASE): $(OBJS_R) $(THIRDPARTY_LIBS)
	@rm -rf $(MERGE_DIR_R)
	@set -e; for lib in $(THIRDPARTY_LIBS); do \
		name=$$(basename $$lib .a); \
		dir=$(MERGE_DIR_R)/$$name; \
		mkdir -p $$dir; \
		(cd $$dir && $(AR) x $$lib); \
	done
	@rm -f $@
	@echo "AR  $@"
	@$(AR) rcs $@ $(OBJS_R) $$(find $(MERGE_DIR_R) -name '*.o')
	@echo "built $@ (size: $$(du -h $@ | cut -f1))"

$(TARGET_DEBUG): $(OBJS_D) $(THIRDPARTY_LIBS)
	@rm -rf $(MERGE_DIR_D)
	@set -e; for lib in $(THIRDPARTY_LIBS); do \
		name=$$(basename $$lib .a); \
		dir=$(MERGE_DIR_D)/$$name; \
		mkdir -p $$dir; \
		(cd $$dir && $(AR) x $$lib); \
	done
	@rm -f $@
	@echo "AR  $@"
	@$(AR) rcs $@ $(OBJS_D) $$(find $(MERGE_DIR_D) -name '*.o')
	@echo "built $@ (size: $$(du -h $@ | cut -f1))"

install: release
	install -d $(PREFIX)/lib $(PREFIX)/include
	install -m 644 $(TARGET_RELEASE) $(PREFIX)/lib/
	cp -r xq $(PREFIX)/include/

clean:
	rm -rf $(BUILD_DIR) $(TARGET_RELEASE) $(TARGET_DEBUG)

-include $(DEPS)
