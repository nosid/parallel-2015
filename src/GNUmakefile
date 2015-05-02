CXX=g++-4.9
CPPFLAGS=-DNDEBUG
CXXFLAGS=-std=c++1y -Wall -Wextra -Werror -O3 -pthread -fdiagnostics-color -march=native
LDFLAGS=-pthread
LDLIBS=-lboost_system

_EXECUTABLES=$(addprefix ../bin/,sync_server async_server async_client)
_HEADERS=$(wildcard *.hpp)

.PHONY: all
all: $(_EXECUTABLES)

.PHONY: clean
clean:
	$(RM) $(_EXECUTABLES)

$(_EXECUTABLES): ../bin/%: %.cpp $(_HEADERS) $(MAKEFILE_LIST)
	$(LINK.cpp) $< $(LOADLIBES) $(LDLIBS) -o $@
