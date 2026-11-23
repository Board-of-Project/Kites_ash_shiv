CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2

SRCS := parser.cpp DAG.cpp heurestic.cpp scheduler.cpp reorder_core.cpp ld_sd_profile.cpp ../src/common/globals.cpp testcase.cpp tester.cpp
OBJS := $(SRCS:.cpp=.o)
TARGET := tester

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp $(wildcard *.h)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean