STATIC ?= 0

UNAME := $(shell uname -a)

# Check for Homebrew
BREW_PREFIX := $(shell command -v brew > /dev/null 2>&1 && brew --prefix)

OCCTINCLUDE := 
OCCLIBPATH :=

# 1. Try Homebrew
ifneq "$(BREW_PREFIX)" ""
    ifneq "$(wildcard $(BREW_PREFIX)/include/opencascade)" ""
        OCCTINCLUDE := $(BREW_PREFIX)/include/opencascade
        OCCLIBPATH := $(BREW_PREFIX)/lib
    endif
endif

# 2. Try /usr/local
ifeq "$(OCCTINCLUDE)" ""
    ifneq "$(wildcard /usr/local/include/opencascade)" ""
        OCCTINCLUDE := /usr/local/include/opencascade
        OCCLIBPATH := /usr/local/lib
    endif
endif

ifneq ($(STATIC),1)
OCCLIBS= \
-lTKXCAF -lTKDESTEP -lTKCDF -lTKDEGLTF -lTKDESTL -lTKDEOBJ \
-lTKBRep -lTKG2d -lTKG3d -lTKGeomBase \
-lTKMath -lTKMesh -lTKXSBase -lTKernel -lTKLCAF
else
LIB_SEARCH_PATH := $(if $(OCCLIBPATH),$(OCCLIBPATH),/usr/local/lib)
OCCLIBS = $(wildcard $(LIB_SEARCH_PATH)/libTK*.a)
endif

ifeq "$(OCCTINCLUDE)" ""

OPENCASCADEINC ?= /usr/include/opencascade
OPENCASCADELIB ?= /usr/lib/opencas

$(info Using default OPENCASCADEINC as "${OPENCASCADEINC}")
$(info Using default OPENCASCADELIB as "${OPENCASCADELIB}")

CXXFLAGS += -I$(OPENCASCADEINC)
LDFLAGS += -L$(OPENCASCADELIB) -L/usr/lib ${OCCLIBS}

else

$(info Using detected OCCTINCLUDE: "${OCCTINCLUDE}")
$(info Using detected OCCLIBPATH: "${OCCLIBPATH}")

CXXFLAGS += -I$(OCCTINCLUDE)
LDFLAGS += -L$(OCCLIBPATH)

ifneq "$(BREW_PREFIX)" ""
    CXXFLAGS += -I$(BREW_PREFIX)/include
    LDFLAGS += -L$(BREW_PREFIX)/lib
endif

CXXFLAGS += -I/usr/local/include/opencascade -I/usr/include
LDFLAGS += -L/usr/local/lib -L/usr/lib
endif

ifneq ($(STATIC),1)
LDFLAGS += $(OCCLIBS)
else
LDFLAGS += -Wl,--start-group -Wl,-Bstatic $(OCCLIBS) -Wl,--end-group
LDFLAGS += -lfontconfig -lfreetype -lexpatw -lpng -lz -Wl,-Bdynamic  -lpthread -ldl
endif

ifeq (Darwin,$(findstring Darwin,$(UNAME)))
CXX=clang++ -std=c++11 -stdlib=libc++
else
CXX=g++
endif

#---------------------------------------------------------------------
#targets
#---------------------------------------------------------------------

ifeq "$(MAKECMDGOALS)" ""
 CXXFLAGS += -ggdb3
endif
ifeq "$(MAKECMDGOALS)" "profile"
 CXXFLAGS += -pg -ggdb3
 LDFLAGS += -pg
endif

# Determine where we should output the object files.
OUTDIR = debug
ifeq "$(MAKECMDGOALS)" "debug"
 OUTDIR = debug
 CXXFLAGS += -ggdb3
endif
ifeq "$(MAKECMDGOALS)" "release"
 OUTDIR = release
 CXXFLAGS += -O3
endif
ifeq "$(MAKECMDGOALS)" "profile"
 OUTDIR = profile
endif

# Add .d to Make's recognized suffixes.
SUFFIXES += .d

# We don't need to clean up when we're making these targets
NODEPS:=clean

#Find all the C++ files in the src/ directory
#SOURCES:=$(shell find * -name '*.cpp' | sort)
SOURCES:=$(shell ls *.cpp | sort)
OBJS:=$(patsubst %.cpp,%.o,$(SOURCES))

#These are the dependency files, which make will clean up after it creates them
DEPFILES:=$(patsubst %.cpp,%.d,$(SOURCES))

#Don't create dependencies when we're cleaning, for instance
ifeq (0, $(words $(findstring $(MAKECMDGOALS), $(NODEPS))))
    -include $(DEPFILES)
endif

EXE = step2gltf

all:	$(EXE)

debug:	$(EXE)

release:$(EXE)

profile:$(EXE)

$(EXE): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

#This is the rule for creating the dependency files
deps/%.d: %.cpp
	$(CXX) $(CXXFLAGS) -MM -MT '$(patsubst src/%,obj/%,$(patsubst %.cpp,%.o,$<))' $< > $@

#This rule does the compilation
obj/%.o: %.cpp %.d %.h
	@$(MKDIR) $(dir $@)
	$(CXX) --static $(CXXFLAGS) -o $@ -c $<

# make clean && svn update
clean:
	bash -c 'rm -f *.o $(OBJS) $(EXE)'

OCCLIBS_DIR = /usr/local/lib
OCCLIBS_FILES = $(patsubst -l%,lib%.so,$(OCCLIBS))

install:
	install -Dp -m 0755 $(EXE) $(DESTDIR)/usr/bin/$(EXE)

uninstall:
	rm -f $(DESTDIR)/usr/bin/$(EXE)

# make debian package
debian-package:
	dpkg-buildpackage -b -rfakeroot -us -uc
