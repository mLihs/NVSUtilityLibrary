# Changelog

All notable changes to NVSUtilityLibrary will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-02-20

### Added
- **CHANGELOG.md** - Initial changelog for release tracking

---

## [1.0.0] - Initial Release

### Features
- Centralized NVS configuration storage for ESP32 Arduino
- JSON serialization per module (moduleId isolation)
- MessagePack storage (recommended) with automatic migration from JSON
- Schema evolution support via JSON flexibility and optional schema_version
- Shared NVS namespace, isolated module keys
