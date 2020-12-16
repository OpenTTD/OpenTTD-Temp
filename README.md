# OpenTTD

## Table of contents

- 1.0) [About](#10-about)
    - 1.1) [Downloading OpenTTD](#11-downloading-openttd)
    - 1.2) [OpenTTD gameplay manual](#12-openttd-gameplay-manual)
    - 1.3) [Supported platforms](#13-supported-platforms)
    - 1.4) [Installing and running OpenTTD](#14-installing-and-running-openttd)
    - 1.5) [Add-on content / mods](#15-add-on-content--mods)
    - 1.6) [OpenTTD directories](#16-openttd-directories)
    - 1.7) [Compiling OpenTTD](#17-compiling-openttd)
- 2.0) [Contact and community](#20-contact-and-community)
    - 2.1) [Contributing to OpenTTD](#21-contributing-to-openttd)
    - 2.2) [Reporting bugs](#22-reporting-bugs)
    - 2.3) [Translating](#23-translating)
- 3.0) [Licensing](#30-licensing)
- 4.0) [Credits](#40-credits)

## 1.0) About

OpenTTD is a transport simulation game based upon the popular game Transport Tycoon Deluxe, written by Chris Sawyer.
It attempts to mimic the original game as closely as possible while extending it with new features.

OpenTTD is licensed under the GNU General Public License version 2.0, but includes some 3rd party software under different licenses.
See the section ["Licensing"](#30-licensing) below for details.

## 1.1) Downloading OpenTTD

OpenTTD can be downloaded from the [official OpenTTD website](https://www.openttd.org/).

Both 'stable' and 'nightly' versions are available for download:

- most people should choose the 'stable' version, as this has been more extensively tested
- the 'nightly' version includes the latest changes and features, but may sometimes be less reliable

On some platforms OpenTTD will also be available via your OS package manager or a similar service.


## 1.2) OpenTTD gameplay manual

OpenTTD has a [community-maintained wiki](https://wiki.openttd.org/), including a gameplay manual and tips.


## 1.3) Supported platforms

OpenTTD has been ported to several platforms and operating systems.

The currently working platforms are:

- FreeBSD (SDL)
- Haiku (SDL)
- Linux (SDL)
- macOS (universal) (Cocoa video and sound drivers)
- OpenBSD (SDL)
- OS/2 (SDL)
- Windows (Win32 GDI (faster) or SDL)

### 1.3.1) Legacy support
Platforms, languages and compilers change.
We'll keep support going on old platforms as long as someone is interested in supporting them, except where it means the project can't move forward to keep up with language and compiler features.

We guarantee that every revision of OpenTTD will be able to load savegames from every older revision (excepting where the savegame is corrupt).
Please report a bug if you find a save that doesn't load.

## 1.4) Installing and running OpenTTD

OpenTTD is usually straightforward to install, but for more help the wiki [includes an installation guide](https://wiki.openttd.org/en/Manual/Installation).

OpenTTD needs some additional graphics and sound files to run.

For some platforms these will be downloaded during the installation process if required.

For some platforms, you will need to refer to [the installation guide](https://wiki.openttd.org/en/Manual/Installation).


### 1.4.1) Free graphics and sound files

The free data files, split into OpenGFX for graphics, OpenSFX for sounds and
OpenMSX for music can be found at:

- https://www.openttd.org/download-opengfx for OpenGFX
- https://www.openttd.org/download-opensfx for OpenSFX
- https://www.openttd.org/download-openmsx for OpenMSX

Please follow the readme of these packages about the installation procedure.
The Windows installer can optionally download and install these packages.


### 1.4.2) Original Transport Tycoon Deluxe graphics and sound files

If you want to play with the original Transport Tycoon Deluxe data files you have to copy the data files from the CD-ROM into the baseset/ directory.
It does not matter whether you copy them from the DOS or Windows version of Transport Tycoon Deluxe.
The Windows install can optionally copy these files.

You need to copy the following files:
- sample.cat
- trg1r.grf or TRG1.GRF
- trgcr.grf or TRGC.GRF
- trghr.grf or TRGH.GRF
- trgir.grf or TRGI.GRF
- trgtr.grf or TRGT.GRF


### 1.4.3) Original Transport Tycoon Deluxe music

If you want the Transport Tycoon Deluxe music, copy the appropriate files from the original game into the baseset folder.
- TTD for Windows: All files in the gm/ folder (gm_tt00.gm up to gm_tt21.gm)
- TTD for DOS: The GM.CAT file
- Transport Tycoon Original: The GM.CAT file, but rename it to GM-TTO.CAT


## 1.5) Add-on content / mods

OpenTTD features multiple types of add-on content, which modify gameplay in different ways.

Most types of add-on content can be downloaded within OpenTTD via the 'Check Online Content' button in the main menu.

Add-on content can also be installed manually, but that's more complicated; the [OpenTTD wiki](https://wiki.openttd.org/) may offer help with that, or the [OpenTTD directory structure guide](./docs/directory_structure.md).

### 1.5.1) AI opponents

OpenTTD comes without AI opponents, so if you want to play with AIs you have to download them.

The easiest way is via the 'Check Online Content' button in the main menu.

You can select some AIs that you think are compatible with your playing style.

AI help and discussions may also be found in the [AI section of the forum](https://www.tt-forums.net/viewforum.php?f=65).

### 1.5.2) Scenarios and height maps

Scenarios and heightmaps can be added via the 'Check Online Content' button in the main menu.

### 1.5.3) NewGRFs

A wide range of add-content is available as NewGRFs, including vehicles, industries, stations, landscape objects, town names and more.

NewGRFs can be added via the 'Check Online Content' button in the main menu.

See also the wiki [guide to NewGRFs](https://wiki.openttd.org/en/Manual/NewGRF) and [the forum graphics development section](https://www.tt-forums.net/viewforum.php?f=66).

### 1.5.4) Game scripts

Game scripts can provide additional challenges or changes to the standard OpenTTD gameplay, for example setting transport goals, or changing town growth behaviour.

Game scripts can be added via the 'Check Online Content' button in the main menu.

See also the wiki [guide to game scripts](https://wiki.openttd.org/en/Manual/Game%20script) and [the forum graphics game script section](https://www.tt-forums.net/viewforum.php?f=65).

### 1.6) OpenTTD directories

OpenTTD uses its own directory structure to store game data, add-on content etc.

For more information, see the [directory structure guide](./docs/directory_structure.md).

### 1.7) Compiling OpenTTD

If you want to compile OpenTTD from source, instructions can be found in [COMPILING.md](./COMPILING.md).


## 2.0) Contact and Community

'Official' channels

- [OpenTTD website](https://www.openttd.org)
- IRC chat using #openttd on irc.oftc.net [more info about our irc channel](https://wiki.openttd.org/en/Development/IRC%20channel)
- [OpenTTD on Github](https://github.com/openTTD/) for code repositories and for reporting issues
- [forum.openttd.org](https://forum.openttd.org/) - the primary community forum site for discussing OpenTTD and related games
- [OpenTTD wiki](https://wiki.openttd.org/) community-maintained wiki, including topics like gameplay guide, detailed explanation of some game mechanics, how to use add-on content (mods) and much more

'Unofficial' channels

- the OpenTTD wiki has a [page listing OpenTTD communities](https://wiki.openttd.org/en/Community/Community) including some in languages other than English


### 2.1) Contributing to OpenTTD

We welcome contributors to OpenTTD.  More information for contributors can be found in [CONTRIBUTING.md](./CONTRIBUTING.md)


### 2.2) Reporting bugs

Good bug reports are very helpful.  We have a [guide to reporting bugs](./CONTRIBUTING.md#bug-reports) to help with this.

Desyncs in multiplayer are complex to debug and report (some software development skils are required).
Instructions can be found in [debugging and reporting desyncs](./docs/debugging_desyncs.md).


### 2.3) Translating

OpenTTD is translated into many languages.  Translations are added and updated via the [online translation tool](https://translator.openttd.org).


## 3.0) Licensing

OpenTTD is licensed under the GNU General Public License version 2.0.
For the complete license text, see the file '[COPYING.md](./COPYING.md)'.
This license applies to all files in this distribution, except as noted below.

Libhydrogen library in `src/3rdparty/libhydrogen` is licensed under the ISC license.
See `src/3rdparty/libhydrogen/LICENSE` for the complete license text.

The squirrel implementation in `src/3rdparty/squirrel` is licensed under the Zlib license.
See `src/3rdparty/squirrel/COPYRIGHT` for the complete license text.

The md5 implementation in `src/3rdparty/md5` is licensed under the Zlib license.
See the comments in the source files in `src/3rdparty/md5` for the complete license text.

The implementations of Posix `getaddrinfo` and `getnameinfo` for OS/2 in `src/3rdparty/os2` are distributed partly under the GNU Lesser General Public License 2.1, and partly under the (3-clause) BSD license.
The exact licensing terms can be found in `src/3rdparty/os2/getaddrinfo.c` resp. `src/3rdparty/os2/getnameinfo.c`.

The implementation of C++17 `std::optional` in `src/3rdparty/optional` is licensed under the Boost Software License - Version 1.0.
See `src/3rdparty/optional/LICENSE_1_0.txt` for the complete license text.


## 4.0 Credits

See [CREDITS.md](./CREDITS.md)
