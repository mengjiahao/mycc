
# OPT ?= -O2 -DNDEBUG # (A) Production use (optimized mode)
OPT ?= -O0 -g2 -Werror #-Wno-unused # (B) Debug mode, w/ full line-level debugging symbols
# OPT ?= -O2 -g2 -DNDEBUG # (C) Profiling mode: opt, but w/debugging symbols
# OPT ?= -O2 -pipe -Wall -W --fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer

#CXX=/opt/compiler/gcc-4.8.2/bin/g++

PROJECT_DIR=.
DEPS_DIR=$(PROJECT_DIR)/deps

# Dependencies
#include $(PROJECT_DIR)/depends.mk

INCLUDE_PATH = -I$(DEPS_DIR)/include

LDFLAGS = -lpthread -lstdc++ -ldl -lrt

SO_LDFLAGS += -rdynamic $(DEPS_LDPATH) $(SO_DEPS_LDFLAGS) -lpthread -lrt -ldl \
              -shared -Wl,--version-script,so-version-script # hide symbol of deps libs

# Compiler
#CXX = g++
CXX = clang

# Compiler opts
GCC_OPTS = -fmax-errors=2
CLANG_OPTS = -ferror-limit=2

# Notes on the flags:
# 1. Added -fno-omit-frame-pointer: perf/tcmalloc-profiler use frame pointers by default
# 2. Added -D__const__= : Avoid over-optimizations of TLS variables by GCC>=4.8, like -D__const__= -D_GNU_SOURCE
# 3. Removed -Werror: Not block compilation for non-vital warnings, especially when the
#    code is tested on newer systems. If the code is used in production, add -Werror back
DFLAGS = -D_REENTRANT -D_THREAD_SAFE -D__USE_LARGEFILE64 -D_FILE_OFFSET_BITS=64 -D_LARGE_FILE -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE 
CXXFLAGS = -Wall -fPIC -std=c++11 -pthread $(DFLAGS) $(OPT)
CFLAGS = -Wall -W -fPIC $(DFLAGS) $(OPT)

ifeq ($(CXX), g++)
CXXFLAGS += $(GCC_OPTS)
else ifeq ($(CXX), clang)
CXXFLAGS += $(CLANG_OPTS)
endif

# Files

SRCEXTS = .c .cc .cpp .proto

ALL_SRCS = $(wildcard $(addprefix $(PROJECT_DIR)/*, $(SRCEXTS)))
ALL_OBJS = $(addsuffix .o, $(basename $(ALL_SRCS)))

PROTO_FILES = $(wildcard src/proto/*.proto)
PROTO_SRCS = $(patsubst %.proto,%.pb.cc, $(PROTO_FILES))
PROTO_HDRS = $(patsubst %.proto,%.pb.h, $(PROTO_FILES))
PROTO_OBJS = $(patsubst %.proto,%.pb.o, $(PROTO_FILES))

OBJS = $(ALL_OBJS)

LIBS =
 
BINS = bloom_test

# Commands

.PHONY:all
all: $(BINS)
	@echo "# Done"

.PHONY:clean
clean:
	@echo "# Clean"
	rm -rf $(ALL_OBJS)
	rm -rf $(BINS)
	rm -rf $(PROTO_SRCS) $(PROTO_HDRS)
	rm -rf *.o

#.SECONDARY: $(PROTO_SRCS)
.PRECIOUS: $(PROTO_SRCS)

# Make
bloom_test: $(OBJS)
	@echo "# Link $@"
	$(CXX) $^ -o $@ $(LDFLAGS)

# Tests	
.PHONY:test
TEST_BINS = 
test: $(TEST_BINS)
	@echo "# Test"

.PHONY:test_clean
test_clean:
	@echo "# Test Clean"
	rm -rf $(TEST_BINS)

# Depends
$(PROTO_OBJS): $(PROTO_HDRS)

# Compile, add @ to echo command
%.pb.cc %.pb.h: %.proto
	@echo "# Protoc gen $@"
	$(PROTOC) --proto_path=$(PROJECT_DIR)/src/proto --proto_path=/usr/local/include --cpp_out=$(PROJECT_DIR)/src/proto/ $<

%.o: %.cc
	@echo "# Compiling cc $@"
	$(CXX) $(CXXFLAGS) $(INCLUDE_PATH) -c $< -o $@

%.o:%.cpp
	@echo "# Compiling cpp $@"
	$(CXX) $(CXXFLAGS) $(INCLUDE_PATH) -c $< -o $@

%.o:%.c
	@echo "# Compiling c $@"
	$(CC) $(CFLAGS) $(INCLUDE_PATH) -c $< -o $@
