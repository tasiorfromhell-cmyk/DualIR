# RottingIR release rules

User requirement: every completed functional change, bug fix or GUI modification
must become a NEW SemVer release after successful tests and Windows x64 Release
build. Several changes in one request may share one release.

- VERSION is the authoritative MAJOR.MINOR.PATCH; CMake reads it.
- Bump PATCH for fixes, MINOR for compatible features, MAJOR for incompatible work.
- Update CHANGELOG.md with only implemented changes, newest version first.
- Never move/delete existing version tags, replace published assets or overwrite releases.
- Preserve product RottingIR, vendor RottingPitStudio, manufacturer code Dlir,
  plugin code DuIR, bundle ID com.dualir.plugin, parameter IDs and DualIRState
  compatibility. Internal DualIR target/class names intentionally remain stable.
- Test the exact commit and build Windows x64 Release before publishing stable.
- Push version tags vMAJOR.MINOR.PATCH for automatic tagged releases. The Windows
  workflow can also bootstrap a missing version tag from a successful main build;
  GITHUB_TOKEN-created tags do not trigger duplicate workflows.
- Each Release contains RottingIR-vVERSION-Windows-x64.zip with the complete
  RottingIR.vst3 bundle. Draft until upload and all validation have succeeded.
- Report old/new versions, actual changes, tests, Windows build, Release URL and
  ZIP filename. Distinguish automated coverage from manual REAPER validation.
