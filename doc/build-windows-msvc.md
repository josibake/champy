# Windows / MSVC Build Guide

This guide describes how to build bitcoind, command-line utilities, and GUI on Windows using Microsoft Visual Studio.

For cross-compiling options, please see [`build-windows.md`](./build-windows.md).

## Preparation

### 1. Install Required Dependencies

The first step is to install the required build applications. The instructions below use WinGet to install the applications.

WinGet is available on all supported Windows versions. The applications mentioned can also be installed manually.

#### Visual Studio

This guide relies on CMake and dependencies installed outside this source
repository.

Minimum required version: Visual Studio 2026 version 18.3 with the "Desktop development with C++" workload.

To install Visual Studio Community Edition with the necessary components, run:

```powershell
winget install --id Microsoft.VisualStudio.Community --override "--wait --quiet --add Microsoft.VisualStudio.Workload.NativeDesktop --add Microsoft.VisualStudio.Component.Git --includeRecommended"
```

This installs:
- Visual Studio
- The "Desktop development with C++" workload (NativeDesktop)
- Git component

After installation, the commands in this guide should be executed in "Developer PowerShell for VS" or "Developer Command Prompt for VS".
The former is assumed hereinafter.

#### Python

Python is required for running the test suite.

To install Python, run:

```powershell
winget install python3
```

### 2. Clone Bitcoin Repository

`git` should already be installed as a component of Visual Studio. If not, download and install [Git for Windows](https://git-scm.com/downloads/win).

Clone the Bitcoin Core repository to a directory. All build scripts and commands will run from this directory.

```powershell
git clone https://github.com/bitcoin/bitcoin.git
```


## Building

CMake will put the resulting object files, libraries, and executables into a dedicated build directory.

In the following instructions, the "Debug" configuration can be specified instead of the "Release" one.

Run `cmake -B build -LH` to see the full list of available options.

### Configure and build

```powershell
cmake -B build -G "Visual Studio 18 2026"
cmake --build build --config Release           # Append "-j N" for N parallel jobs.
ctest --test-dir build --build-config Release  # Append "-j N" for N parallel tests.
cmake --install build --config Release         # Optional.
```

## Performance Notes

### Antivirus Software

To improve the build process performance, one might add the Bitcoin repository directory to the Microsoft Defender Antivirus exclusions.
