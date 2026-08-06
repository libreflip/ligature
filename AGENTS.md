# Repository Guidelines

## Project Structure & Module Organization

This is a PlatformIO Arduino project for the MKS ESP32 FOC board. Firmware entry points live under `src/`; the current `src/hall_validation/main.cpp` target validates the motor Hall signals without enabling either bridge. Put shared headers in `include/` and project-local libraries in `lib/<name>/`. Add PlatformIO test suites under `test/<suite>/`. Hardware procedures and agent conventions live in `docs/`, while `docs/pdf/` contains reference datasheets and schematics. Treat `docs/plan/ligature.md` as the behavioral and safety reference for production firmware.

## Build, Test, and Development Commands

The default PlatformIO environment is `hall-validation`:

```sh
/Users/hrmny/.platformio/penv/bin/pio run
/Users/hrmny/.platformio/penv/bin/pio run -t upload
/Users/hrmny/.platformio/penv/bin/pio device monitor -e hall-validation
/Users/hrmny/.platformio/penv/bin/pio test -e hall-validation
```

These commands build, flash, open the 460800-baud serial monitor, and run PlatformIO tests, respectively. Upload and monitor commands require connected hardware. Follow `docs/hall-validation.md` for the guided manual revolution check and the separate `simplefoc-hall-validation` environment.

## Coding Style & Naming Conventions

Match the existing Arduino C++ style: two-space indentation, braces on the declaration line, and `const` parameters where useful. Use `PascalCase` for structs, `lowerCamelCase` for functions and variables, and descriptive `kPascalCase` names for constants. Keep interrupt handlers short, allocation-free, and marked `IRAM_ATTR` when required by the ESP32. No formatter or linter is configured, so preserve nearby formatting and keep comments focused on hardware intent or safety constraints.

## Testing Guidelines

There is currently no automated coverage requirement and no test implementation beyond `test/README`. Name new suites by behavior, for example `test/hall_sequence/test_main.cpp`. Build before flashing, add host-testable logic where practical, and report compilation separately from physical hardware validation. Never claim a Hall or motion fix is validated without the documented device test.

## Commit & Pull Request Guidelines

Use Conventional Commits with a short imperative summary: `<type>[optional scope]: <description>`, for example `docs: update firmware wayfinding for Ligature spec` or `fix(hall): reject invalid transition`. Use the narrowest fitting type (`feat`, `fix`, `docs`, `test`, `refactor`, `chore`) and keep each change focused. Pull requests should explain the hardware behavior affected, list build/test evidence, link the relevant `.scratch/` issue, and include serial output when behavior is device-dependent.

## Agent skills

### Issue tracker

Issues and specs are tracked as local Markdown files under `.scratch/`. See `docs/agents/issue-tracker.md`.

### Triage labels

The five canonical triage roles use their default label strings. See `docs/agents/triage-labels.md`.

### Domain docs

This repository uses a single-context domain-doc layout. See `docs/agents/domain.md`.
