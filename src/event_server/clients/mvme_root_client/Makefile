ROOTCFLAGS	=	$(shell root-config --cflags)
ROOTLIBS	=	$(shell root-config --libs)
ROOTVERSION =   $(shell root-config --version)
ROOTPREFIX  =   $(shell root-config --prefix)

mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(dir $(mkfile_path))

CXXFLAGS   +=	-I$(MVME)/include -I$(MVME)/include/mvme -I.
CXXFLAGS   +=	-O3

# If -std=c++11 is set by root-config force c++14 here. Newer ROOT versions set
# c++14 or c++17 depending on how they were built.
ifneq "$(findstring -std=c++11, ${ROOTCFLAGS})" ""
CXXFLAGS += -std=c++14
endif

.PHONY: all clean print_root_version

all: print_root_version libmvme_root_event.so mvme_root_client

EVENT_OBJECTS_HEADERS = $(current_dir)mvme_root_event_objects.h $(current_dir)mvme_root_event_objects_LinkDef.h
EVENT_OBJECTS_DEPS = $(current_dir)mvme_root_event_objects.cxx $(EVENT_OBJECTS_HEADERS)
TEMPLATE_FILES = $(wildcard templates/*.mustache)

print_root_version:
	echo "Compiling against ROOT-${ROOTVERSION} found in ${ROOTPREFIX}"

mvme_root_event_rdict.cxx: $(EVENT_OBJECTS_DEPS)
	rootcling -f $@ -rml libmvme_root_event.so -rmf mvme_root_event.rootmap $(EVENT_OBJECTS_HEADERS)

libmvme_root_event.so: $(EVENT_OBJECTS_DEPS) mvme_root_event_rdict.cxx
	$(CXX) $(ROOTCFLAGS) $(CXXFLAGS) $(ROOTLIBS) -shared -fPIC -o $@ $< mvme_root_event_rdict.cxx

mvme_root_client: $(current_dir)mvme_root_client.cc libmvme_root_event.so $(TEMPLATE_FILES)
	$(CXX) $(LDFLAGS) $(ROOTCFLAGS) $(CXXFLAGS) $< '-Wl,-rpath,$$ORIGIN/:$$ORIGIN/../lib' libmvme_root_event.so $(ROOTLIBS) -o $@

clean:
	-rm -f libmvme_root_event.so mvme_root_event_rdict.cxx mvme_root_event_rdict_rdict.pcm
	-rm -f mvme_root_client mvme_root_event.rootmap

install: all
	mkdir -p $(MVME)/bin $(MVME)/lib $(MVME)/include
	install -m 755 mvme_root_client $(MVME)/bin/mvme_root_client
	install -m 755 libmvme_root_event.so $(MVME)/lib/libmvme_root_event.so
	install -m 644 mvme_root_event_rdict_rdict.pcm $(MVME)/lib/mvme_root_event_rdict_rdict.pcm
	install -m 644 $(EVENT_OBJECTS_HEADERS) $(MVME)/include/mvme
