# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.2.0] - 2026-08-27

### Added
- Push-to-talk recording support for desktop environments implementing the `org.freedesktop.portal.GlobalShortcuts` portal.

### Fixed
- Wayland/GNOME autostart crash by implementing native D-Bus `org.kde.StatusNotifierItem` system tray integration.
- Status notifier unit test execution when D-Bus session bus is unavailable in headless environments.

## [1.1.0] - 2026-08-26

### Added
- Credential storage fallback mechanism and on-demand API key loading for environments without system keyring support.

### Changed
- Replaced Qt Widgets dependencies with pure Qt Quick and QML components.
- Optimized QML codebase and property bindings for Ahead-Of-Time (AOT) compilation and improved startup performance.
- Cleaned up redundant codebase helpers and modernized internal pipelines.

### Fixed
- Speech-to-text engine switching between online and offline modes and resolved startup mode initialization.

## [1.0.0] - 2026-08-23

### Added
- Initial public release of QTranscribe.
- Pre-built distribution packages for Debian/Ubuntu (`.deb`), Fedora (`.rpm`) and Arch Linux (`.pkg.tar.zst`).

[Unreleased]: https://github.com/Vidhan31/qtranscribe/compare/v1.2.0...HEAD
[1.2.0]: https://github.com/Vidhan31/qtranscribe/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/Vidhan31/qtranscribe/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/Vidhan31/qtranscribe/releases/tag/v1.0.0
