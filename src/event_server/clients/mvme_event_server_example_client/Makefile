mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(dir $(mkfile_path))

CXXFLAGS   +=	-I$(MVME)/include -I$(MVME)/include/mvme
CXXFLAGS   +=	-std=c++14

.PHONY: all clean

all: mvme_event_server_example_client

mvme_event_server_example_client: $(current_dir)mvme_event_server_example_client.cc
	$(CXX) $(LDFLAGS) $(CXXFLAGS) $< '-Wl,-rpath,$$ORIGIN/:$$ORIGIN/../lib' -o $@

clean:
	-rm -f mvme_event_server_example_client

install: all
	mkdir -p $(MVME)/bin
	install -m 755 mvme_event_server_example_client $(MVME)/bin/mvme_event_server_example_client
