CXXFLAGS  =-std=c++11 $(shell pkg-config --cflags libndn-cxx)  -pthread
LDFLAGS =-std=c++11 $(shell pkg-config --libs libndn-cxx) -Wall -D __MACOSX_CORE__ -framework CoreMIDI -framework CoreAudio -framework CoreFoundation -pthread
CXX = g++
CC = $(CXX)
CONTROLLER = ControllerMIDI
PLAYBACKMODULE = PlaybackModuleMIDI


app: $(CONTROLLER) $(PLAYBACKMODULE)

$(CONTROLLER): $(CONTROLLER).o
	$(CXX) $(LDFLAGS) $(CONTROLLER).o RtMidi.cpp -o $(CONTROLLER)

$(PLAYBACKMODULE): $(PLAYBACKMODULE).o
	$(CXX) $(LDFLAGS) $(PLAYBACKMODULE).o RtMidi.cpp -o $(PLAYBACKMODULE)

$(CONTROLLER).o:
	$(CXX) $(CXXFLAGS) -c -o $(CONTROLLER).o $(CONTROLLER).cpp

$(PLAYBACKMODULE).o:
	$(CXX) $(CXXFLAGS) -c -o $(PLAYBACKMODULE).o $(PLAYBACKMODULE).cpp



clean:
	rm -Rf $(CONTROLLER) $(PLAYBACKMODULE) *.o
