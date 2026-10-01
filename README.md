# FreeDV GUI — Enhanced Client

This repository is a personal development fork of the [FreeDV GUI](https://github.com/drowe67/freedv-gui) project, focused on modernizing the user interface and exploring additional capabilities for digital voice operation.

It is based on the upstream FreeDV `v3.0-dev` development branch and retains the underlying FreeDV functionality while adding experimental and user-focused enhancements.

> **Important:** This is an independent fork and is not an official FreeDV release. For official FreeDV software, documentation, downloads, and support, visit [freedv.org](https://freedv.org/) and the [official FreeDV GUI repository](https://github.com/drowe67/freedv-gui).

## What's Different in This Fork?

The integrated version combines two major areas of development: a modernized user interface and real-time decoded speech transcription.

### Modernized User Interface

The interface has been updated while retaining the existing FreeDV operating workflow and capabilities.

Enhancements include:

- Modernized application layout and presentation
- Light, dark, and system appearance support
- Refined display settings
- More compact use of screen space
- Existing multi-pane/notebook workspace support retained
- Direct display controls retained and refined
- Improved clock/time display behavior
- Compatibility handling for multiple wxWidgets versions

The goal is to make FreeDV more comfortable to use on modern desktops without fundamentally changing how an experienced FreeDV operator uses the application.

### Decoded Speech Transcription

This fork adds an experimental decoded-speech transcription pipeline using [whisper.cpp](https://github.com/ggml-org/whisper.cpp).

The transcription system includes:

- Real-time processing of decoded speech audio
- Asynchronous audio handling to avoid blocking the primary FreeDV audio path
- Voice Activity Detection (VAD)
- Whisper-based speech-to-text transcription
- Integration of transcription into the FreeDV client
- Cross-platform Whisper build support for Windows, Linux, and macOS

Speech transcription is intended as an additional operating aid. It does not replace the decoded audio output and, like any speech-recognition system, may produce incorrect or incomplete text depending on signal quality, noise, speech characteristics, and decoder performance.

## Integrated Development Branch

The `personal/integrated` branch combines the completed UI modernization and decoded-speech development work into a single version.

Development was performed in separate feature branches and then integrated after local testing and CI validation.

The integrated build is currently validated by GitHub Actions on:

- Windows
- Linux
- macOS

## Relationship to FreeDV

FreeDV is an open-source digital voice system for HF radio developed by the FreeDV community.

This repository builds upon that work. The intent of this fork is to experiment with user-interface improvements and additional operator tools while continuing to track upstream FreeDV development.

Where practical, generally useful fixes and improvements may be suitable for contribution back to the upstream project.

### Official FreeDV Resources

- [FreeDV Website](https://freedv.org/)
- [Official FreeDV GUI Repository](https://github.com/drowe67/freedv-gui)
- [FreeDV GUI User Manual](USER_MANUAL.md)

## Building

The original FreeDV GUI build instructions have been retained in:

**[BUILDING.md](BUILDING.md)**

Those instructions cover Linux, Windows cross-compilation, macOS, audio driver selection, installation, and profile-guided optimization.

Because this fork includes additional dependencies for decoded-speech transcription, build requirements may evolve as the feature is developed.

## Releases

Tagged releases from this repository represent tested snapshots of this enhanced fork.

They should not be confused with official FreeDV releases distributed by the FreeDV project.

Release notes will identify the integrated features and the upstream development baseline used for each release.

## Project Status

This fork is under active development and should be considered experimental.

The integrated branch has been built and tested locally and through the repository's Windows, Linux, and macOS CI workflows. Additional real-world testing of the user-interface and speech-transcription features is ongoing.

## License and Attribution

This project is derived from the FreeDV GUI project and retains the licensing and attribution requirements of the upstream project and its included dependencies.

See the repository license files and third-party documentation for details.

FreeDV and the original FreeDV GUI project are the work of their respective developers and contributors.
