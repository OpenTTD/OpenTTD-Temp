#[=======================================================================[.rst:
FindZSTD
-------

Finds the ZSTD library.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``ZSTD_FOUND``
  True if the system has the ZSTD library.
``ZSTD_INCLUDE_DIRS``
  Include directories needed to use ZSTD.
``ZSTD_LIBRARIES``
  Libraries needed to link to ZSTD.
``ZSTD_VERSION``
  The version of the ZSTD library which was found.

Cache Variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``ZSTD_INCLUDE_DIR``
  The directory containing ``zstd.h``.
``ZSTD_LIBRARY``
  The path to the ZSTD library.

#]=======================================================================]

find_package(PkgConfig QUIET)
pkg_check_modules(PC_ZSTD QUIET libzstd)

find_path(ZSTD_INCLUDE_DIR
    NAMES zstd.h
    PATHS ${PC_ZSTD_INCLUDE_DIRS}
)

find_library(ZSTD_LIBRARY
    NAMES zstd zstd_static
    PATHS ${PC_ZSTD_LIBRARY_DIRS}
)

# With vcpkg, the library path should contain both 'debug' and 'optimized'
# entries (see target_link_libraries() documentation for more information)
#
# NOTE: we only patch up when using vcpkg; the same issue might happen
# when not using vcpkg, but this is non-trivial to fix, as we have no idea
# what the paths are. With vcpkg we do. And we only official support vcpkg
# with Windows.
if(VCPKG_TOOLCHAIN AND ZSTD_LIBRARY)
    if(ZSTD_LIBRARY MATCHES "/debug/")
        set(ZSTD_LIBRARY_DEBUG ${ZSTD_LIBRARY})
        string(REPLACE "/debug/lib/" "/lib/" ZSTD_LIBRARY_RELEASE ${ZSTD_LIBRARY})
    else()
        set(ZSTD_LIBRARY_RELEASE ${ZSTD_LIBRARY})
        string(REPLACE "/lib/" "/debug/lib/" ZSTD_LIBRARY_DEBUG ${ZSTD_LIBRARY})
        # Also fix the name of debug file
        string(REPLACE "." "d." ZSTD_LIBRARY_DEBUG ${ZSTD_LIBRARY_DEBUG})
    endif()
    include(SelectLibraryConfigurations)
    select_library_configurations(ZSTD)
endif()

set(ZSTD_VERSION ${PC_ZSTD_VERSION})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZSTD
    FOUND_VAR ZSTD_FOUND
    REQUIRED_VARS
        ZSTD_LIBRARY
        ZSTD_INCLUDE_DIR
    VERSION_VAR ZSTD_VERSION
)

if(ZSTD_FOUND)
    set(ZSTD_LIBRARIES ${ZSTD_LIBRARY})
    set(ZSTD_INCLUDE_DIRS ${ZSTD_INCLUDE_DIR})
endif()

mark_as_advanced(
    ZSTD_INCLUDE_DIR
    ZSTD_LIBRARY
)
