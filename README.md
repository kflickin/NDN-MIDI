# ndn-midi

Recommended Applications to install and to use in conjunction:

* [vmpk](http://vmpk.sourceforge.net/) - to replace an actual electronic keyboard for MIDI input (ControllerMIDI)
* [SimpleSynth](http://notahat.com/simplesynth/) - a simple synthesizer program for MIDI playback (PlaybackModuleMIDI)

Usage:

Use `make` to compile.

To launch the playback module, you need to give it a name:

```
./PlaybackModuleMIDI <playback-module-name>
```

To launch the controller, you need to provide the name of the playback module you want to connect to, and give yourself a name:

```
./ControllerMIDI <playback-module-name> <controller-name>
```

If you connect remotely, controller will need to know playback module's name, but playback module doesn't have to know controllers' name.
