ROOTCFLAGS	=	$(shell root-config --cflags)
ROOTLIBS	=	$(shell root-config --libs)
CXXFLAGS   +=	-std=c++14
# To make things work inside the source tree
CXXFLAGS   +=	-I.. -I../../../3rdparty -std=c++14
# Installation time include path using $MVME from the environment
CXXFLAGS   +=	-I$(MVME)/include

.PHONY: all clean

all: libmvme_root_event.so mvme_root_client

EVENT_OBJECTS_HEADERS = mvme_root_event_objects.h mvme_root_event_objects_LinkDef.h
EVENT_OBJECTS_DEPS = mvme_root_event_objects.cxx $(EVENT_OBJECTS_HEADERS)
TEMPLATE_FILES = $(wildcard templates/*.mustache)

mvme_root_event_rdict.cxx: $(EVENT_OBJECT_DEPS)
	rootcling -f $@ -rml libmvme_root_event.so -rmf mvme_root_event.rootmap $(EVENT_OBJECTS_HEADERS)

libmvme_root_event.so: $(EVENT_OBJECTS_DEPS) mvme_root_event_rdict.cxx
	$(CXX) $(ROOTCFLAGS) $(CXXFLAGS) $(ROOTLIBS) -shared -fPIC -o $@ $<

mvme_root_client: mvme_root_client.cc libmvme_root_event.so ../common/event_server_lib.h ../common/event_server_proto.h $(TEMPLATE_FILES)
	$(CXX) $(LDFLAGS) $(ROOTCFLAGS) $(CXXFLAGS) -Wl,-rpath,. $(ROOTLIBS) libmvme_root_event.so -o $@ $<

clean:
	-rm -f libmvme_root_event.so mvme_root_event_rdict.cxx mvme_root_event_rdict_rdict.pcm
	-rm -f mvme_root_client mvme_root_event.rootmap
