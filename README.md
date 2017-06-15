# ndn-midi

Usage:

Use `make` to compile.

To launch the playback module, you need to give it a name:

```

./PlaybackModuleMIDI <playback-module-name>

```

To launch the controller, you need to provide the name of the playback module you want to connect to (i.e. you need to know playback module's name), and give yourself a name (but playback module doesn't have to know your name):

```

./ControllerMIDI <playback-module-name> <controller-name>

```
