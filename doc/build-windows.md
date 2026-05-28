WINDOWS BUILD NOTES
====================

Below are some notes on how to build Bitcoin Core for Windows.

The options known to work for building Bitcoin Core on Windows are:

* On Linux, using the [Mingw-w64](https://www.mingw-w64.org/) cross compiler tool chain.
* On Windows, using [Windows Subsystem for Linux (WSL)](https://learn.microsoft.com/en-us/windows/wsl/about) and Mingw-w64.
* On Windows, using [Microsoft Visual Studio](https://visualstudio.microsoft.com). See [`build-windows-msvc.md`](./build-windows-msvc.md).

Other options which may work, but which have not been extensively tested are (please contribute instructions):

* On Windows, using a POSIX compatibility layer application such as [cygwin](https://www.cygwin.com/) or [msys2](https://www.msys2.org/).

The instructions below work on Ubuntu and Debian. Make sure the distribution's `g++-mingw-w64-x86-64-posix`
package meets the minimum required GCC version specified in [dependencies.md](dependencies.md).

Installing Windows Subsystem for Linux
---------------------------------------

Follow the upstream installation instructions, available [here](https://learn.microsoft.com/en-us/windows/wsl/install).

Cross-compilation for Ubuntu and Windows Subsystem for Linux
------------------------------------------------------------

The steps below can be performed on Ubuntu or WSL. Install the cross toolchain
and dependencies through your system package manager or an external packaging
repository. See [dependencies.md](dependencies.md) for a complete overview.

Acquire the source in the usual way:

    git clone https://github.com/bitcoin/bitcoin.git
    cd bitcoin

Note that for WSL the Bitcoin Core source path should be somewhere in the
default mount file system, for example `/usr/src/bitcoin`, and not under
`/mnt/d/`. Building from the host Windows file system is significantly slower
and can expose path handling differences.

Build using:

    cmake -B build -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++

Run `cmake -B build -LH` to see the full list of available options.

    cmake --build build     # Append "-j N" for N parallel jobs.

Installation
-------------

After building using the Windows subsystem it can be useful to copy the compiled
executables to a directory on the Windows drive in the same directory structure
as they appear in the release `.zip` archive. This can be done in the following
way. This will install to `c:\workspace\bitcoin`, for example:
```shell
cmake --install build --prefix /mnt/c/workspace/bitcoin
```

Note that due to the presence of debug information, the binaries may be very large,
if you do not need the debug information, you can prune it during install by calling:
```shell
cmake --install build --prefix /mnt/c/workspace/bitcoin --strip
```
