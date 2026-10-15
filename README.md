# CIGA — Lost Signal

CIGA — Lost Signal is a custom scene transition plugin for OBS Studio. It combines a signal-loss visual effect with bundled audio and seasonal themes.

## Features

- Custom signal-loss scene transition.
- Integrated audio support.
- Bundled Normal, Halloween, November and Christmas audio.
- Windows x64 installer.
- CMake-based source project.

## Compatibility and testing status

- **OBS Studio 32.x, Windows x64:** tested by updating an existing installation.
- **Clean OBS 32.x installation:** not yet independently verified.
- **OBS Studio 33+ layout:** installer logic is included, but this layout has not yet been independently validated.
- Portable OBS installations have not been verified.

Compatibility claims will be updated as more installation configurations are tested.

## Installation

1. Close OBS Studio completely.
2. Download `CIGA-Lost-Signal-Setup-v0.1.0.exe` from the project's GitHub Releases page.
3. Run the installer and approve the requested Windows permissions.
4. Reopen OBS Studio.
5. Find **CIGA - Lost Signal** in the scene transition list and test it.

If the transition does not appear, check the OBS log for plugin-loading errors.

## Building from source

The project uses CMake and the OBS plugin build system. The required OBS and dependency versions are defined in `buildspec.json`. Build configuration is provided by `CMakeLists.txt` and `CMakePresets.json`.

For general build-system guidance, see the [OBS Plugin Template documentation](https://github.com/obsproject/obs-plugintemplate/wiki).

The Windows x64 installer is built with Inno Setup. Packaging instructions are in `tools/package-release.ps1` and `installer/CIGA-Lost-Signal.iss`.

## Project files

- `src/` — plugin source code.
- `data/` — transition effect, image, audio and locale resources.
- `installer/` — Windows installer configuration.
- `tools/` — release packaging script.
- `cmake/` — CMake build support.
- `LICENSE` — applicable project licence.
- `PRIVACY_POLICY.md` — privacy notice for this project.
- `THIRD_PARTY_NOTICES.md` and `ASSET_CREDITS.md` — provenance and licensing notes.

## Privacy

The plugin is designed to run locally within OBS Studio. See [PRIVACY_POLICY.md](PRIVACY_POLICY.md) for the privacy notice covering this release.

## Issues and contributions

Please use the repository's Issues section to report bugs or suggest improvements. Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.

## Licence

See [LICENSE](LICENSE) for the licence terms. This project started from the OBS Plugin Template and retains its existing licence file. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for context.

CIGA — Lost Signal is an independent project and is not affiliated with or endorsed by the OBS Project.
