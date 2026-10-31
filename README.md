# FxRack

VST3 multi-effect with four slots. Each slot holds one effect (or stays empty) and can be switched
on/off. The signal passes through the slots from left to right; the arrows in each slot swap it with
its neighbour.

Effects: Leslie, Octaver, Delay, Reverb, Chorus, Vibrato, Filter, Distortion.

## Build

Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.8 is downloaded automatically.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Result: build/FxRack_artefacts/Release/VST3/FxRack.vst3

Install: Windows C:\Program Files\Common Files\VST3\ , macOS ~/Library/Audio/Plug-Ins/VST3/ , Linux ~/.vst3/

JUCE is licensed under AGPLv3 or a commercial license.
