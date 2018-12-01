CXX := clang++
CXXFLAGS = -Wall -g -std=c++11
OBJS = cfg.o main.o
TARGET = cfg
TESTS_SRC = $(shell find tests -name '*.c')
TESTS = $(patsubst %.c,%.bin,$(TESTS_SRC))

all: $(TARGET) $(TESTS) Makefile

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -lcapstone -o $@

%.o: %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@

tests/%.bin: tests/%.c
	gcc -O0 -c $< -o tests/$*.o
	objcopy -j .text -O binary tests/$*.o $@

clean:
	rm -rf $(TARGET) $(OBJS) $(TESTS)

.PHONY: all tests clean
