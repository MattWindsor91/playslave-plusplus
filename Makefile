# This file is part of playd.
# playd is licenced under the MIT license: see LICENSE.txt.

#
# This is the GNU Makefile for playd.
#

## BEGIN USER-CHANGEABLE VARIABLES ##

# Where to put the object files and other intermediate fluff.
builddir ?= build

# Where the source resides.
srcdir ?= src

# The warning flags to use when building playd.
WARNS ?= -Wall -Wextra -Werror

# Programs used during building.
CC         ?= clang
CXX        ?= clang++
GZIP       ?= gzip -9 --stdout
INSTALL    ?= install
PKG_CONFIG ?= pkg-config
FORMAT     ?= clang-format -i
DOXYGEN    ?= doxygen
GIT        ?= git
GROFF_HTML ?= groff -Thtml -mdoc

# The C standard used to compile playd.
# This should usually be 'c99'.
C_STD ?= c99

# The C++ standard used to compile playd.
# This should usually be 'c++11', but older compilers might need
# 'c++0x'.
CXX_STD ?= c++11

# Variables used to decide where to install playd and its man pages.
prefix      ?= /usr/local
bindir      ?= $(prefix)/bin
mandir      ?= /usr/share/man/man1

## END USER-CHANGEABLE VARIABLES ##

# The name of the program.  This is hardcoded in several places, such as the
# man page, so users are not recommended to change it.
NAME = playd

# Calculate the path to the outputted program.
BIN = $(builddir)/$(NAME)

# Where the raw man page is, and where its processed forms should go.
MAN_SRC   = $(srcdir)/$(NAME).1
MAN_GZ    = $(builddir)/$(NAME).1.gz
MAN_HTML  = $(builddir)/$(NAME).1.html

# This should include all of the source directories for playd,
# excluding any special ones defined below.  The root source directory is
# implied.
OWN_SUBDIRS = audio io player
SUBDIRS     = $(OWN_SUBDIRS) contrib/pa_ringbuffer

# Now we work out which libraries to use, using pkg-config.
# These packages are always used: the PortAudio C library, libsox, and libuv.
PKGS = portaudio-2.0 sox libuv

# PortAudio's C++ bindings aren't always available, so we bundle them.
# However, if there is a PortAudioCPP package available, we can make use of it
# instead.
HAS_PORTAUDIOCPP ?= $(shell $(PKG_CONFIG) --list-all | grep 'portaudiocpp')
ifeq "$(HAS_PORTAUDIOCPP)" ""
  # Add the bundled bindings to the list of things to compile.
  PORTAUDIOCPP_DIR  = contrib/portaudiocpp
  CXXFLAGS         += -I"$(srcdir)/$(PORTAUDIOCPP_DIR)"
  SUBDIRS          += $(PORTAUDIOCPP_DIR)
else
  PKGS += portaudiocpp
endif

# Set up the flags needed to use the packages.
PKG_CFLAGS  += `$(PKG_CONFIG) --cflags $(PKGS)`
PKG_LDFLAGS += `$(PKG_CONFIG) --libs $(PKGS)`

# Now we make up the source and object directory sets...
SRC_SUBDIRS = $(srcdir) $(addprefix $(srcdir)/,$(SUBDIRS))
OBJ_SUBDIRS = $(builddir) $(builddir)/tests $(addprefix $(builddir)/,$(SUBDIRS))

# ...And find the sources to compile and the objects they make.
SOURCES  = $(foreach dir,$(SRC_SUBDIRS),$(wildcard $(dir)/*.cpp))
OBJECTS  = $(patsubst $(srcdir)%,$(builddir)%,$(SOURCES:.cpp=.o))
CSOURCES = $(foreach dir,$(SRC_SUBDIRS),$(wildcard $(dir)/*.c))
COBJECTS = $(patsubst $(srcdir)%,$(builddir)%,$(CSOURCES:.c=.o))

# When running unit tests, we add in the test objects, defined below.
# Note that main.o is NOT built during testing, because it contains the main
# entry point.
TEST_SOURCES  = $(wildcard $(srcdir)/tests/*.cpp)
TEST_OBJECTS  = $(patsubst $(srcdir)%,$(builddir)%,$(TEST_SOURCES:.cpp=.o))
TEST_OBJECTS += $(filter-out $(builddir)/main.o,$(OBJECTS))
TEST_BIN      = $(builddir)/$(NAME)_test

# These are used for source transformations, such as formatting.
# We don't want to disturb contributed source with these.
OWN_SRC_SUBDIRS = $(srcdir) $(addprefix $(srcdir)/,$(OWN_SUBDIRS))
OWN_SOURCES     = $(foreach dir,$(OWN_SRC_SUBDIRS),$(wildcard $(dir)/*.cpp))
OWN_CSOURCES    = $(foreach dir,$(OWN_SRC_SUBDIRS),$(wildcard $(dir)/*.c))
OWN_HEADERS     = $(foreach dir,$(OWN_SRC_SUBDIRS),$(wildcard $(dir)/*.hpp))
OWN_CHEADERS    = $(foreach dir,$(OWN_SRC_SUBDIRS),$(wildcard $(dir)/*.h))
TO_FORMAT       = $(OWN_SOURCES) $(OWN_CSOURCES) $(OWN_HEADERS) $(OWN_CHEADERS)

# Version stuff
CFLAGS += -D PD_VERSION=\"$(shell git describe --tags --always)\"

# Now set up the flags needed for playd.
CFLAGS   += -c $(WARNS) $(PKG_CFLAGS) -g -std=$(C_STD)
CXXFLAGS += -c $(WARNS) $(PKG_CFLAGS) -g -std=$(CXX_STD)
LDFLAGS  += $(PKG_LDFLAGS)

## BEGIN RULES ##

.PHONY: clean mkdir install format gh-pages doc coverage

all: mkdir $(BIN) man

#
# Program
#

# Rule for making playd itself.
$(BIN): $(COBJECTS) $(OBJECTS)
	@echo LINK $@
	@$(CXX) $(COBJECTS) $(OBJECTS) $(LDFLAGS) -o $@

# Rule for compiling C code.
$(builddir)/%.o: $(srcdir)/%.c
	@echo CC $@
	@$(CC) $(CFLAGS) $< -o $@

# Rule for compiling C++ code.
$(builddir)/%.o: $(srcdir)/%.cpp
	@echo CXX $@
	@$(CXX) $(CXXFLAGS) $< -o $@

#
# Man pages
#

# Rule for making the man pages.
man: $(MAN_GZ)
# Rule for making compressed versions of man pages.
$(builddir)/%.1.gz: $(srcdir)/%.1
	@echo GZIP $@
	@< $< $(GZIP) > $@

# Rule for making HTML versions of man pages.
$(builddir)/%.1.html: $(srcdir)/%.1
	@echo 'GROFF(html)' $@
	@< $< $(GROFF_HTML) > $@

#
# Documentation
#

# Updates the GitHub Pages documentation.
gh-pages: doc $(MAN_HTML)
	env MAN_HTML=$(MAN_HTML) ./make_gh_pages.sh

# Builds the documentation, using doxygen.
doc:
	$(DOXYGEN)

#
# Tests
#

test: mkdir $(TEST_BIN)
	@echo TEST
	@$(TEST_BIN)

$(TEST_BIN): $(COBJECTS) $(TEST_OBJECTS)
	@echo LINK $@
	@$(CXX) $(COBJECTS) $(TEST_OBJECTS) $(LDFLAGS) -o $@

#
# Special targets
#

# Cleans up the results of a previous build.
clean:
	@echo CLEAN
	@rm -f $(OBJECTS) $(COBJECTS) $(MAN_HTML) $(MAN_GZ) $(BIN)
	@rm -f $(TEST_OBJECTS) $(TEST_BIN)

# Makes the build subdirectories.
mkdir:
	mkdir -p $(OBJ_SUBDIRS)

# Installs the compiled binary to `bindir`, and the man page to `mandir`.
install: $(BIN) $(MAN_GZ)
	$(INSTALL) $(BIN) $(bindir)
	-$(INSTALL) $(MAN_GZ) $(mandir)

# Runs a formatter over the sources.
format: $(TO_FORMAT)
	@echo FORMAT $^
	@$(FORMAT) $^

coverage: CXXFLAGS += -fprofile-arcs -ftest-coverage
coverage: LDFLAGS += -fprofile-arcs -ftest-coverage
coverage: mkdir test
