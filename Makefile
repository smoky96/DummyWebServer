CC		:= g++
CXXFLAGS	:= -std=c++17 -Wall -Wextra -g

BIN		:= bin
SRC		:= src
INCLUDE	:= include
LIB		:= lib

LIBRARIES	:= -lrt -pthread

EXECUTABLE1	:= server
EXECUTABLE2	:= cgi
EXECUTABLE3	:= stress
SOURCEDIRS	:= $(SRC)
SOURCEDIRS1	:= $(shell find $(SRC)/server -type d)
SOURCEDIRS2	:= $(shell find $(SRC)/cgi -type d)
SOURCEDIRS3	:= $(shell find $(SRC)/stress -type d)
INCLUDEDIRS	:= $(shell find $(INCLUDE) -type d)
LIBDIRS		:= $(shell find $(LIB) -type d)

CINCLUDES	:= $(patsubst %,-I%, $(INCLUDEDIRS:%/=%))
CLIBS		:= $(patsubst %,-L%, $(LIBDIRS:%/=%))

SOURCES		:= $(wildcard $(patsubst %,%/*.cpp, $(SOURCEDIRS)))
SOURCES1		:= $(wildcard $(patsubst %,%/*.cpp, $(SOURCEDIRS1)))
SOURCES2		:= $(wildcard $(patsubst %,%/*.cpp, $(SOURCEDIRS2)))
SOURCES3		:= $(wildcard $(patsubst %,%/*.cpp, $(SOURCEDIRS3)))
OBJECTS1		:= $(SOURCES:.cpp=.o) $(SOURCES1:.cpp=.o)
OBJECTS2		:= $(SOURCES:.cpp=.o) $(SOURCES2:.cpp=.o)
OBJECTS3		:= $(SOURCES:.cpp=.o) $(SOURCES3:.cpp=.o)

all: $(BIN)/$(EXECUTABLE1) $(BIN)/$(EXECUTABLE2) $(BIN)/$(EXECUTABLE3)
.PHONY: all

.PHONY: clean
clean:
	-$(RM) $(BIN)/$(EXECUTABLE1)
	-$(RM) $(BIN)/$(EXECUTABLE2)
	-$(RM) $(BIN)/$(EXECUTABLE3)
	-$(RM) $(OBJECTS1)
	-$(RM) $(OBJECTS2)
	-$(RM) $(OBJECTS3)


run: all
	./$(BIN)/$(EXECUTABLE)

$(BIN)/$(EXECUTABLE1): $(OBJECTS1)
	$(CC) $(CXXFLAGS) $(CLIBS) $^ -o $@ $(LIBRARIES)

$(BIN)/$(EXECUTABLE2): $(OBJECTS2)
	$(CC) $(CXXFLAGS) $(CLIBS) $^ -o $@ $(LIBRARIES)

$(BIN)/$(EXECUTABLE3): $(OBJECTS3)
	$(CC) $(CXXFLAGS) $(CLIBS) $^ -o $@ $(LIBRARIES)

%.o: %.cpp
	$(CC) $(CXXFLAGS) $(CINCLUDES) -c -o $@ $<