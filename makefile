CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS    = -lbz2

LIB_SRC  = DataLoader.cpp ASGraph.cpp Solver.cpp General.cpp
HEADERS  = $(wildcard *.h)
TESTS    = test_dataloader test_asgraph test_solver test_general

all: ASGraph_Solver

ASGraph_Solver: $(LIB_SRC) main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LIB_SRC) main.cpp $(LDLIBS) -o $@

test_dataloader: DataLoader.cpp test_dataloader.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) DataLoader.cpp test_dataloader.cpp $(LDLIBS) -o $@

test_asgraph: DataLoader.cpp ASGraph.cpp test_asgraph.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) DataLoader.cpp ASGraph.cpp test_asgraph.cpp $(LDLIBS) -o $@

test_solver: DataLoader.cpp ASGraph.cpp Solver.cpp test_solver.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) DataLoader.cpp ASGraph.cpp Solver.cpp test_solver.cpp $(LDLIBS) -o $@

test_general: $(LIB_SRC) test_general.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) $(LIB_SRC) test_general.cpp $(LDLIBS) -o $@

# Compila ed esegue tutti i test dei moduli
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done

clean:
	rm -f ASGraph_Solver $(TESTS)

.PHONY: all test clean