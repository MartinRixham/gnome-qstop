#* qstop makefile

MAKEFILE_VERSION = 1.0

E = \033[
RST = $(E)0m
BLD = $(E)1m

RED     = $(E)91m
GREEN   = $(E)92m
YELLOW  = $(E)93m
BLUE    = $(E)94m
MAGENTA = $(E)95m
CYAN    = $(E)96m
WHITE   = $(E)97m

B_RED     = $(BLD)$(RED)
B_GREEN   = $(BLD)$(GREEN)
B_YELLOW  = $(BLD)$(YELLOW)
B_BLUE    = $(BLD)$(BLUE)
B_MAGENTA = $(BLD)$(MAGENTA)
B_CYAN    = $(BLD)$(CYAN)
B_WHITE   = $(BLD)$(WHITE)

cecho = printf "%b" "$(1)"

override QSTOP_VERSION := $(shell head -n100 src/qstop.cpp 2>/dev/null | grep "Version =" | cut -f2 -d"\"" || echo " unknown")

ifneq ($(QUIET),true)
	override QUIET := false
endif

PREFIX ?= /usr/local

#? Detect PLATFORM and ARCH from uname if not set
PLATFORM ?= $(shell uname -s || echo unknown)
ARCH ?= $(shell uname -m || echo unknown)

override PLATFORM_LC := $(shell echo $(PLATFORM) | tr '[:upper:]' '[:lower:]')

ifneq ($(PLATFORM_LC),linux)
$(error $(shell printf "\033[1;91mERROR: \033[97mPlatform $(PLATFORM) not supported, qstop requires GNOME on Linux!\033[0m"))
endif

#? Use all CPU cores (will only be set if using Make 4.3+)
ifeq ($(shell expr $(MAKE_VERSION) \>= 4.3 2>/dev/null || echo 0),1)
	override THREADS := $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)
	override MAKEFLAGS += -j$(THREADS) --output-sync=target
endif

CXX ?= g++

#? Pull in the correct compiler flags for debug or release builds
ifeq ($(DEBUG),true)
	override OPTFLAGS := -O0 -g
else
	override OPTFLAGS := -O2 -DNDEBUG
endif

#? The Directories, Source, Includes, Objects and Binary
SRCDIR		:= src
INCDIRS		:= include $(wildcard lib/**/include)
BUILDDIR	:= obj
TARGETDIR	:= bin
SRCEXT		:= cpp
DEPEXT		:= d
OBJEXT		:= o

#? Filter out unsupported compiler flags
override GOODFLAGS := $(shell for flag in -fstack-clash-protection -fcf-protection; do $(CXX) -Werror $$flag -x c++ -c /dev/null -o /dev/null >/dev/null 2>&1 && echo "$$flag"; done)

#? Flags, Libraries and Includes
override REQFLAGS   := -std=c++23
WARNFLAGS			:= -Wall -Wextra -pedantic
OPTFLAGS			+= -ftree-vectorize -flto=auto
LDCXXFLAGS			:= -pthread -D_GLIBCXX_ASSERTIONS -D_FILE_OFFSET_BITS=64 $(GOODFLAGS) $(ADDFLAGS)
override CXXFLAGS	+= $(REQFLAGS) $(LDCXXFLAGS) $(OPTFLAGS) $(WARNFLAGS)
override LDFLAGS	+= $(LDCXXFLAGS) $(OPTFLAGS) $(WARNFLAGS)
INC					:= $(foreach incdir,$(INCDIRS),-isystem $(incdir)) -I$(SRCDIR)

SOURCES	:= $(sort $(shell find $(SRCDIR) -maxdepth 1 -type f -name *.$(SRCEXT)))
SOURCES += $(sort $(shell find $(SRCDIR)/$(PLATFORM_LC) -type f -name *.$(SRCEXT)))

override OBJECTS := $(patsubst $(SRCDIR)/%,$(BUILDDIR)/%,$(SOURCES:.$(SRCEXT)=.$(OBJEXT)))

#? Default Make
.ONESHELL:
all: | info directories qstop

info:
	@$(call cecho,$(B_MAGENTA)qstop$(RST)$(BLD) v$(QSTOP_VERSION)$(RST) $(E)3;38;5;240mMakefile v$(MAKEFILE_VERSION)$(RST)\n\n)
	@printf "\033[1;92mPLATFORM   \033[1;93m?| \033[0m$(PLATFORM)\n"
	@printf "\033[1;96mARCH       \033[1;93m?| \033[0m$(ARCH)\n"
	@printf "\033[1;93mCXX        \033[1;93m?| \033[0m$(CXX) \033[1;93m(\033[97m$(shell $(CXX) -dumpfullversion -dumpversion || echo unknown)\033[93m)\n"
	@printf "\033[1;94mTHREADS    \033[1;94m:| \033[0m$(THREADS)\n"
	@printf "\033[1;92mREQFLAGS   \033[1;91m!| \033[0m$(REQFLAGS)\n"
	@printf "\033[1;91mWARNFLAGS  \033[1;94m:| \033[0m$(WARNFLAGS)\n"
	@printf "\033[1;94mOPTFLAGS   \033[1;94m:| \033[0m$(OPTFLAGS)\n"
	@printf "\033[1;93mLDCXXFLAGS \033[1;94m:| \033[0m$(LDCXXFLAGS)\n"
	@printf "\033[1;95mCXXFLAGS   \033[1;92m+| \033[0;37m\$$(\033[92mREQFLAGS\033[37m) \$$(\033[93mLDCXXFLAGS\033[37m) \$$(\033[94mOPTFLAGS\033[37m) \$$(\033[91mWARNFLAGS\033[37m)\n"
	@printf "\033[1;95mLDFLAGS    \033[1;92m+| \033[0;37m\$$(\033[93mLDCXXFLAGS\033[37m) \$$(\033[94mOPTFLAGS\033[37m) \$$(\033[91mWARNFLAGS\033[37m)\n"
	@printf "\n"

help:
	@printf " $(BANNER)\n"
	@printf "\033[1;97mqstop makefile\033[0m\n"
	@printf "usage: make [argument]\n\n"
	@printf "arguments:\n"
	@printf "  all          Compile qstop (default argument)\n"
	@printf "  clean        Remove built objects\n"
	@printf "  distclean    Remove built objects and binaries\n"
	@printf "  install      Install qstop to \$$PREFIX ($(PREFIX))\n"
	@printf "  uninstall    Uninstall qstop from \$$PREFIX\n"
	@printf "  info         Display information about Environment,compiler and linker flags\n"
	@printf "  test         Build and run the googletest suite (GTEST_DIR=$(GTEST_DIR))\n"

#? Make the Directories
directories:
	@mkdir -p $(TARGETDIR)
	@mkdir -p $(BUILDDIR)/$(PLATFORM_LC)

#? Clean only Objects
clean:
	@printf "\033[1;91mRemoving: \033[1;97mbuilt objects...\033[0m\n"
	@rm -rf $(BUILDDIR)

#? Clean Objects and Binaries
distclean: clean
	@printf "\033[1;91mRemoving: \033[1;97mbuilt binaries...\033[0m\n"
	@rm -rf $(TARGETDIR)

install:
	@printf "\033[1;92mInstalling binary to: \033[1;97m$(DESTDIR)$(PREFIX)/bin/qstop\n"
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@cp -p $(TARGETDIR)/qstop $(DESTDIR)$(PREFIX)/bin/qstop
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/qstop
	@printf "\033[1;92mInstalling doc to: \033[1;97m$(DESTDIR)$(PREFIX)/share/qstop\n"
	@mkdir -p $(DESTDIR)$(PREFIX)/share/qstop
	@cp -p README.md $(DESTDIR)$(PREFIX)/share/qstop
	@printf "\033[1;92mInstalling themes to: \033[1;97m$(DESTDIR)$(PREFIX)/share/qstop/themes\033[0m\n"
	@cp -pr themes $(DESTDIR)$(PREFIX)/share/qstop

uninstall:
	@printf "\033[1;91mRemoving: \033[1;97m$(DESTDIR)$(PREFIX)/bin/qstop\033[0m\n"
	@rm -rf $(DESTDIR)$(PREFIX)/bin/qstop
	@printf "\033[1;91mRemoving: \033[1;97m$(DESTDIR)$(PREFIX)/share/qstop\033[0m\n"
	@rm -rf $(DESTDIR)$(PREFIX)/share/qstop

#? Pull in dependency info for *existing* .o files
-include $(OBJECTS:.$(OBJEXT)=.$(DEPEXT))

#? Link
.ONESHELL:
qstop: $(OBJECTS) | directories
	@sleep 0.2 2>/dev/null || true
	@TSTAMP=$$(date +%s 2>/dev/null || echo "0")
	@$(QUIET) || printf "\n\033[1;92mLinking and optimizing binary\033[37m...\033[0m\n"
	@$(CXX) -o $(TARGETDIR)/qstop $^ $(LDFLAGS) || exit 1
	@printf "\033[1;92m100$(CUR_LEFT)\033[1;97m qstop \033[0m -> \033[1;97m$(TARGETDIR)/qstop\033[0m \033[1;93m(\033[97m$$(du -ah $(TARGETDIR)/qstop | cut -f1)iB\033[93m)\033[0m\n"

#? Compile
.ONESHELL:
$(BUILDDIR)/%.$(OBJEXT): $(SRCDIR)/%.$(SRCEXT) | directories
	@sleep 0.3 2>/dev/null || true
	@$(QUIET) || printf "\033[1;97mCompiling $<\033[0m\n"
	@$(CXX) $(CXXFLAGS) $(INC) -MMD -c -o $@ $< || exit 1

#? Tests, googletest is built from source as distributions ship it (Debian/Ubuntu: googletest package)
GTEST_DIR		?= /usr/src/googletest/googletest
TESTDIR			:= tests
TEST_SOURCES	:= $(sort $(wildcard $(TESTDIR)/*.$(SRCEXT)))
override TEST_OBJECTS := $(patsubst $(TESTDIR)/%,$(BUILDDIR)/$(TESTDIR)/%,$(TEST_SOURCES:.$(SRCEXT)=.$(OBJEXT)))
override GTEST_OBJECTS := $(BUILDDIR)/$(TESTDIR)/gtest-all.$(OBJEXT) $(BUILDDIR)/$(TESTDIR)/gtest_main.$(OBJEXT)
override TEST_CXXFLAGS := $(CXXFLAGS) -isystem $(GTEST_DIR)/include -DQSTOP_TEST_FIXTURES=\"$(CURDIR)/$(TESTDIR)/fixtures\"

-include $(TEST_OBJECTS:.$(OBJEXT)=.$(DEPEXT))

test: $(TARGETDIR)/qstop_tests
	@$(TARGETDIR)/qstop_tests

$(TARGETDIR)/qstop_tests: $(TEST_OBJECTS) $(GTEST_OBJECTS) $(filter-out $(BUILDDIR)/main.$(OBJEXT),$(OBJECTS)) | directories
	@$(QUIET) || printf "\033[1;92mLinking tests\033[37m...\033[0m\n"
	@$(CXX) -o $@ $^ $(LDFLAGS) || exit 1

$(BUILDDIR)/$(TESTDIR)/%.$(OBJEXT): $(TESTDIR)/%.$(SRCEXT) | directories
	@mkdir -p $(BUILDDIR)/$(TESTDIR)
	@$(QUIET) || printf "\033[1;97mCompiling $<\033[0m\n"
	@$(CXX) $(TEST_CXXFLAGS) $(INC) -MMD -c -o $@ $< || exit 1

$(BUILDDIR)/$(TESTDIR)/gtest%.$(OBJEXT): $(GTEST_DIR)/src/gtest%.cc | directories
	@mkdir -p $(BUILDDIR)/$(TESTDIR)
	@$(QUIET) || printf "\033[1;97mCompiling $<\033[0m\n"
	@$(CXX) $(REQFLAGS) $(LDCXXFLAGS) $(OPTFLAGS) -isystem $(GTEST_DIR)/include -I$(GTEST_DIR) -c -o $@ $< || exit 1

#? Non-File Targets
.PHONY: all info help clean distclean install uninstall directories test
