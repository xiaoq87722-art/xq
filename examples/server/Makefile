.PHONY: clean all run debug

CXX = g++
BASE_CXXFLAGS = -std=c++20 -O2 -g -fno-omit-frame-pointer -fno-stack-clash-protection -march=native
INCLUDE = -I../../ -I/usr/local/include/mimalloc-3.2
LDFLAGS = -L/usr/local/lib -L/usr/local/lib/mimalloc-3.2
BASE_LIBS = -lspdlog -lmimalloc

TARGET = server

SRCS = main.cpp \
       $(wildcard ../../src/net/*.cpp) \
       $(wildcard ../../src/utils/*.cpp)

OBJS = $(SRCS:.cpp=.o)

# 默认 release: 带 -DNDEBUG, 不链接 profiler
CXXFLAGS = $(BASE_CXXFLAGS) -DNDEBUG
LIBS = $(BASE_LIBS)

all: $(TARGET)

# debug: 不带 NDEBUG, 启用 gperftools profiler (火焰图)
debug:
	$(MAKE) clean
	$(MAKE) all CXXFLAGS='$(BASE_CXXFLAGS)' LIBS='$(BASE_LIBS) -lprofiler'

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)
