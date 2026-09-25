# A8Manager

A tool to manage Presets and Sample files for the Rossum-Electro Assimil8or

This branch contains the [3.0.0 workspace update](UI-PREVIEW.md) with scalable
controls, direct value dragging, waveform controls, and sequential zone editing.

New to the app? Start with the [User Guide](USER-GUIDE.md) for a first-preset
walkthrough, mouse gestures, sample/loop markers, audition speed and pitch,
stereo assignments, and saving your work.

Windows and macOS builds available at: https://cpr2323.github.io/a8manager/index.html

# Building

The JUCE and oolib submodules must be initialised before the first build:

```
git submodule update --init --recursive
```

Then configure and build with CMake 3.24 or newer:

```
cmake -B cmake_build
cmake --build cmake_build --config Release
```

Pitch-preserving audition uses MIT-licensed Signalsmith Stretch
1.4.0 and Signalsmith Linear 0.6.4. CMake downloads pinned revisions on the first
configure (internet access required), then reuses its build-tree cache. The
dependency notices are included in built applications. See [UI-PREVIEW.md](UI-PREVIEW.md)
for playback modes, limitations and build details.

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
