# Upstream MrDrums tests

These are MrDrums' original tests, kept for reference and attribution. They
are **not** run by CI and several no longer apply as written:

- The `*_sample_path_*`, `*_preset_*` and state-roundtrip tests assume a sample
  is loaded the moment `set_param` returns. FlukeDrums loads samples on a worker
  thread, so those tests need a "wait for the worker" step (see `settle()` in
  `../reroll_workflow_test.cpp`).
- `module_identity_test.sh` and the release-layout tests check for the
  `mrdrums` module id and paths; FlukeDrums' id is `flukedrums`.

Porting them is tracked in `TASKS.md`. The FlukeDrums test suite is
`../run_tests.sh`.
