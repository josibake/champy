This directory contains integration tests that test bitcoind and its
utilities in their entirety. It does not contain unit tests, which
can be found in [/src/test](/src/test), etc.

This directory contains the following sets of tests:

- [fuzz](/test/fuzz) A runner to execute all fuzz targets from
  [/src/test/fuzz](/src/test/fuzz).

The fuzz tests can be run as explained in the sections below.

# Running tests locally

Before tests can be run locally, Champy must be built.

## Fuzz tests

Run `build/test/fuzz/test_runner.py` after building the fuzz binary.
