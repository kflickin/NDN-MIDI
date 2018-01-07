# NDN-MIDI

### Dependencies

Named-Data Networking:

* [ndn-cxx](https://github.com/named-data/ndn-cxx) - NDN C++ library with eXperimental eXtensions
* [NFD](https://github.com/named-data/NFD) - NDN Forwarding Daemon

We recommend the following MIDI applications for testing:

* [vmpk](http://vmpk.sourceforge.net/) - to replace an actual electronic keyboard for MIDI input (used by ControllerMIDI)
* [SimpleSynth](http://notahat.com/simplesynth/) - a simple synthesizer program for MIDI playback (used by PlaybackModuleMIDI)

### Usage

Use `make` to compile.

To enable the 2 applications to send packets to each other, launch the NDN Forwarding Daemon by `nfd-start`.

To launch the playback module, you need to give it a name:

```
./PlaybackModuleMIDI <playback-module-name> [optional-project-name]
```

To launch the controller, you need to provide the name of the playback module you want to connect to, and give yourself a name:

```
./ControllerMIDI <playback-module-name> <controller-name> [optional-project-name]
```

For additional configuration and usage information, see ndnmidi.pdf
