.PHONY: clean all run

CXX      = g++
CXXFLAGS = -std=c++20 -O3 -DNDEBUG -march=native
TARGET   = client

all: $(TARGET)

$(TARGET): main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)