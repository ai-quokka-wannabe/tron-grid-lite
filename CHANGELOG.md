# Changelog

All notable changes to TronGrid Lite are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Initial project scaffold: build system, CI, editor config, and hello world.
- Full project infrastructure inherited from [TronGrid](https://github.com/MatejGomboc/tron_grid):
  linting configs (clang-tidy, markdownlint), governance documents (contributing guide,
  code of conduct, security policy, style guide), issue and PR templates, CI workflows
  (main, PR validation, release, cache cleanup), and Claude assistant commands.
- Internal static libraries inherited from TronGrid: `testing`, `signals`, `logging`,
  `math`, and `window` (Win32 / XCB), each with their own test suites.
- `src/` layout with GPL v3 licence headers; volk translation unit included in the build.
