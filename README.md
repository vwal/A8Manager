# A8Manager

A tool to manage Presets and Sample files for the Rossum-Electro Assimil8or

This branch contains the [3.0.0 workspace update](UI-PREVIEW.md) with scalable
controls, direct value dragging, waveform controls, and sequential zone editing.
It also includes a **Waveform designer** for single-cycle audio,
CV/modulation and linked layer banks such as supersaws. Both workspaces share
the preset slots and unsaved edits. Generate & Assign writes unique 24-bit WAVs
and a recipe into the current folder; the designer's Save writes the selected
preset and creates a self-contained `PNN - <name>` copy for the SD card.
Original/shared samples remain in place; place the complete named copy directly
under the card root, not its loose contents. Standalone
package export remains available with a selectable preset number.
**Save/Export Bank** combines saved presets from one or more explicitly selected
folders into a new flat hardware folder. Originals stay untouched; duplicate
preset slots must be resolved before export. Open the exported bank to work in
one flat folder, with ordinary in-place saves from both workspaces. See
[bank export](USER-GUIDE.md#savingexporting-a-preset-bank).
The designer's **Test output...** action exports a separate hardware-validation
package: the current design or audio/CV test signals, a low-level timing reference
on its own channel, and an exact-frame manifest for recordings. It does not start
computer playback or change the current preset. Every test output defaults to
Mix Off; CV still requires speaker-disconnected, DC-capable measurement equipment.
Audio cycles and complete layer banks can be auditioned live while shaping;
the compact waveform preview also has an expanded view.
Generated CV files are marked to prevent Samples-workspace speaker audition,
and known CV channels lock Mix and Mix modulation Off for individual-output use
(verify on hardware). CV and audio cannot be assigned to zones in the same channel.
See the [waveform workspace guide](WAVEFORM-DESIGNER.md) and
[hardware test checklist](HARDWARE-TEST-CHECKLIST.md).
The [CV-generation review](CV-GENERATION-REVIEW.md) compares the supplied discussion
with current capabilities and records the remaining companion-channel design work.

New to the app? Start with the [User Guide](USER-GUIDE.md) for a first-preset
walkthrough, mouse gestures, sample/loop markers, audition speed and pitch,
stereo assignments, and saving your work.

Historical upstream Windows and macOS builds (not this independent fork):
https://cpr2323.github.io/a8manager/index.html

# Building

The JUCE and oolib submodules must be initialised before the first build (and
updated after pulling dependency changes). This version pins JUCE **9.0.2**,
including its WAV final-padding and CoreAudio fixes:

```
git submodule update --init --recursive
```

Then configure and build with CMake 3.24 or newer and a C++20-capable compiler.
CMake requires standard C++20 (without compiler extensions) for the application
and regression tests:

```
cmake -B cmake_build
cmake --build cmake_build --config Release
```

Pitch-preserving audition uses MIT-licensed Signalsmith Stretch
1.4.0 and Signalsmith Linear 0.6.4. CMake downloads pinned revisions on the first
configure (internet access required), then reuses its build-tree cache. The
dependency notices are included in built applications. See [UI-PREVIEW.md](UI-PREVIEW.md)
for playback modes, limitations and build details.

Audio saw/pulse generation also uses a small, pinned MIT-licensed
[DaisySP PolyBLEP excerpt](Source/ThirdParty/DaisySP/README.md), vendored in the
source tree. It complements the existing oversampling/harmonic filtering and
requires no additional download or hardware dependency. Its license is packaged
with the application; CV generation and its exact DC levels remain unchanged.

Optional parser/CV and stereo split regression probes are documented in
[tests/README.md](tests/README.md).

# Windows

There are no special steps to installing on Windows.

# OSX

Since the application is not signed (I don't want to pay the $99/yr) you will have to do a manual step in the console to allow it to run.

1. Download A8Manager.zip, the app will be automatically extracted during download.
2. Use the following command to allow it to run
3. **_xattr -d com.apple.quarantine ~/Downloads/A8Manager.app_**
4. You can now run it. You can move it to the Applications folder if you do desire.

# Linux

There is no Linux version yet, but since we are using JUCE it should be _relative simple_. I actually did a quick test of this back in 2023 and published the results in a youtube video.
[A8Manager Linux build verification video](https://www.youtube.com/watch?v=fk4RRMh7hZc)

# Thanks

Thanks to Shawn Rakestraw for helping out with reverse engineering, some coding, documentation, testing, etc.

Thanks to Jean Obuchowicz for the great icon!

![](<Source/GUI/Assimil8or/Data/377906243_984640136172516_2914152204379747274_n.png>)
