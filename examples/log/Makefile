.PHONY: clean all

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Werror
INCLUDE = -I../../
LDFLAGS = -L/usr/local/lib
LIBS = -lspdlog

TARGET = log
SRCS = main.cpp ../../src/utils/log.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	./$(TARGET)
