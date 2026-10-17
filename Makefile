# Makefile — builds and tests the translator.
#
#   make        build release/clean
#   make test   build, then run the end-to-end tests (test/run_e2e.sh)
#   make clean  remove the binary (yes — `make clean` cleans `clean`)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic

BIN  := release/clean
SRCS := src/main.cpp src/lexer.cpp src/parser.cpp src/check.cpp src/toposort.cpp src/emitter.cpp
HDRS := src/diag.h src/ast.h src/lexer.h src/parser.h src/check.h src/toposort.h src/emitter.h

.PHONY: all test clean

all: $(BIN)

$(BIN): $(SRCS) $(HDRS) | release
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

release:
	mkdir -p release

test: all
	bash test/run_e2e.sh

clean:
	rm -f $(BIN)
