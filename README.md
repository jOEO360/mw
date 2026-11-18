# FatBass – VST3 / AU "Ghosts n Stuff"-style saw/square bass

## Build
Requires CMake 3.22+, Git, and a compiler (Visual Studio 2022 on Windows, Xcode on macOS).

    cmake -B build
    cmake --build build --config Release

JUCE 8 is downloaded automatically. Plugins are copied to your system plugin folders after build
(VST3 on both OSes, AU on macOS). On macOS you get a universal (Apple Silicon + Intel) binary.

## Signal flow
Mono legato voice -> up to 7x unison saw + PWM square (PolyBLEP, anti-aliased)
 -> 2x oversampled: drive -> 24dB nonlinear ladder LPF (fast env)
                    \-> parallel "sizzle": pre-filter high band, hard saturated
 -> LR4 crossover: lows summed to mono + clean sine sub + asymmetric warmth saturation
 -> tempo-synced kick duck (lows ducked harder than highs) -> soft clip

## Old Mac / no Xcode? Two options

### A) Build in the cloud (no local toolchain at all) - recommended
1. Create a free GitHub account and a new repository.
2. Upload everything in this folder (including the hidden `.github` folder) and commit.
3. Open the repo's **Actions** tab -> "Build FatBass" -> wait ~10 min.
4. Download the `FatBass-macos-latest` and `FatBass-windows-latest` artifacts.
   - Mac: copy `FatBass.vst3` to `/Library/Audio/Plug-Ins/VST3/` and `FatBass.component`
     to `/Library/Audio/Plug-Ins/Components/`, then run once in Terminal:
     `xattr -cr /Library/Audio/Plug-Ins/VST3/FatBass.vst3 /Library/Audio/Plug-Ins/Components/FatBass.component`
     (the build is unsigned, so macOS quarantines it otherwise).
   - Windows: copy `FatBass.vst3` to `C:\Program Files\Common Files\VST3\`.
   Builds are Intel + Apple Silicon universal. Needs macOS 10.13+ to run.

### B) Build locally with just the Command Line Tools
    xcode-select --install        # or download an older CLT from developer.apple.com/download/all
    brew install cmake            # or the installer from cmake.org
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
This targets JUCE 7.0.12, which supports older compilers than JUCE 8.
