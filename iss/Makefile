# xtc-iss: instruction-set simulator of the whole SoC (used by `make trace`).
#   make -C iss      ->  iss/build/xtc-iss

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

SRCS := $(wildcard src/*.cpp)
HDRS := $(wildcard src/*.h)

build/xtc-iss: $(SRCS) $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -rf build

.PHONY: clean
