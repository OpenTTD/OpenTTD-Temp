# Compiling OpenTTD

## Required/optional libraries

The following libraries are used by OpenTTD for:

- zlib: (de)compressing of old (0.3.0-1.0.5) savegames, content downloads,
   heightmaps
- liblzo2: (de)compressing of old (pre 0.3.0) savegames
- liblzma: (de)compressing of savegames (1.1.0 and later)
- libzstd: (de)compressing of savegames (1.11.0 and later)
- libpng: making screenshots and loading heightmaps
- libfreetype: loading generic fonts and rendering them
- libfontconfig: searching for fonts, resolving font names to actual fonts
- libicu: handling of right-to-left scripts (e.g. Arabic and Persian) and
   natural sorting of strings (Linux only)
- libSDL2: hardware access (video, sound, mouse) (not required for Windows or macOS)

OpenTTD does not require any of the libraries to be present, but without
liblzma you cannot open most recent savegames and without zlib you cannot
open most older savegames or use the content downloading system.
Without libSDL/liballegro on non-Windows and non-macOS machines you have
no graphical user interface; you would be building a dedicated server.

## Windows:

You need Microsoft Visual Studio 2017 or more recent.

You can download the free Visual Studio Community Edition from Microsoft at
https://visualstudio.microsoft.com/vs/community/.

OpenTTD needs the Platform SDK, if it isn't installed already. This can be
done during installing Visual Studio, by selecting
`Visual C++ MFC for x86 and x64` (and possibly
`Visual C++ ATL for x86 and x64` depending on your version). If not, you
can get download it as [MS Windows Platform SDK](https://developer.microsoft.com/en-US/windows/downloads/windows-10-sdk).

Install the SDK by following the instructions as given.

Dependencies for OpenTTD on Windows are handled via
[vcpkg](https://github.com/Microsoft/vcpkg/). First you need to install vcpkg
by following the `Quick Start` instructions of their
[README](https://github.com/Microsoft/vcpkg/blob/master/README.md).

After this, you can install the dependencies OpenTTD needs. We advise to use
the `static` versions, and OpenTTD currently needs the following dependencies:

- liblzma
- libzstd
- libpng
- lzo
- zlib

To install both the x64 (64bit) and x86 (32bit) variants (though only one is necessary), you can use:

```ps
.\vcpkg install liblzma:x64-windows-static zstd:x64-windows-static libpng:x64-windows-static lzo:x64-windows-static zlib:x64-windows-static
.\vcpkg install liblzma:x86-windows-static zstd:x86-windows-static libpng:x86-windows-static lzo:x86-windows-static zlib:x86-windows-static
```

You can open the folder (as a CMake project). CMake will be detected, and you can compile from there.
If libraries are installed but not found, you need to set VCPKG_TARGET_TRIPLET in CMake parameters.
For Visual Studio 2017 you also need to set CMAKE_TOOLCHAIN_FILE.
(Typical values are shown in the MSVC project file command line example)

Alternatively, you can create a MSVC project file via CMake. For this
either download CMake from https://cmake.org/download/ or use the version
that comes with vcpkg. After that, you can run something similar to this:

```powershell
mkdir build
cd build
cmake.exe .. -G'Visual Studio 16 2019' -DCMAKE_TOOLCHAIN_FILE="<location of vcpkg>\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET="x64-windows-static"
```

Change `<location of vcpkg>` to where you have installed vcpkg. After this
in the build folder are MSVC project files. MSVC can rebuild the project
files himself via the `ZERO_CHECK` project.

## All other platforms
Minimum required version of CMake is 3.9.

```bash
mkdir build
cd build
cmake ..
make
```

For more information on how to use CMake (including how to make Release builds),
we urge you to read [their excellent manual](https://cmake.org/cmake/help/latest/guide/user-interaction/index.html).

## Supported compilers

Every compiler that is supported by CMake and supports C++17, should be
able to compile OpenTTD. As the exact list of compilers changes constantly,
we refer to the compiler manual to see if it supports C++17, and to CMake
to see if it supports your compiler.

## Compilation of base sets

To recompile the extra graphics needed to play with the original Transport
Tycoon Deluxe graphics you need GRFCodec (which includes NFORenum) as well.
GRFCodec can be found at
https://www.openttd.org/downloads/grfcodec-releases/latest.html.

Having GRFCodec installed can cause regeneration of the `.grf` files, which
are written in the source directory. This can leave your repository in a
modified state, as different GRFCodec versions can cause binary differences
in the resulting `.grf` files. Also translations might have been added for
the base sets which are not yet included in the base set information files.
To avoid this behaviour, disable GRFCodec (and NFORenum) in CMake cache
(`GRFCODEC_EXECUTABLE` and `NFORENUM_EXECUTABLE`).

## Developers settings

You can control some flags directly via `CXXFLAGS` (any combination
of these flags will work fine too):

- `-DRANDOM_DEBUG`: this helps with debugging desyncs.
- `-fno-inline`: this avoids creating inline functions; this can make
  debugging a lot easier.
- `-O0`: this disables all optimizations; this can make debugging a
  lot easier.
- `-p`: this enables profiling.

Always use a clean buildfolder if you changing `CXXFLAGS`, as this
value is otherwise cached. Example use:

`CXXFLAGS="-fno-inline" cmake ..`
