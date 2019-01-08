ROOTCFLAGS	=	$(shell root-config --cflags)
ROOTLIBS	=	$(shell root-config --libs)
CXXFLAGS   +=   -I.. -I../../../3rdparty -std=c++14

.PHONY: all clean

all: libmvme_root_event.so mvme_root_client

mvme_root_event_rdict.cxx: mvme_root_event_objects.cxx mvme_root_event_objects.h mvme_root_event_objects_LinkDef.h
	rootcling -f mvme_root_event_rdict.cxx -rml libmvme_root_event.so -rmf mvme_root_event.rootmap mvme_root_event_objects.h mvme_root_event_objects_LinkDef.h

libmvme_root_event.so: mvme_root_event_objects.cxx mvme_root_event_objects.h mvme_root_event_objects_LinkDef.h  mvme_root_event_rdict.cxx
	$(CXX) $(ROOTCFLAGS) $(CXXFLAGS) $(ROOTLIBS) -shared -fPIC -o $@ $< mvme_root_event_rdict.cxx

mvme_root_client: mvme_root_client.cc libmvme_root_event.so ../common/event_server_lib.h ../common/event_server_proto.h templates/user_objects.h.mustache templates/user_objects.cxx.mustache
	$(CXX) $(LDFLAGS) $(ROOTCFLAGS) $(CXXFLAGS) -Wl,-rpath,. $(ROOTLIBS) libmvme_root_event.so -o $@ $<

clean:
	-rm -f libmvme_root_event.so mvme_root_event_rdict.cxx mvme_root_event_rdict_rdict.pcm
	-rm -f mvme_root_client.o mvme_root_client mvme_root_event.rootmap
