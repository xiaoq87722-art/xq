.PHONY: clean all run

CXX = g++
CXXFLAGS = -std=c++20 -O2 -g -fno-omit-frame-pointer -fno-stack-clash-protection -DNDEBUG -march=native
INCLUDE = -I../../ -I/usr/local/include/mimalloc-3.2
LDFLAGS = -L/usr/local/lib -L/usr/local/lib/mimalloc-3.2
LIBS = -lspdlog -lmimalloc -lprofiler

TARGET = server

SRCS = main.cpp \
       $(wildcard ../../src/net/*.cpp) \
       $(wildcard ../../src/utils/*.cpp)

OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(LIBS)
	rm -f $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)