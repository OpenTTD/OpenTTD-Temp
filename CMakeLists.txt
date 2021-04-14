cmake_minimum_required(VERSION 3.9)

if(NOT BINARY_NAME)
    set(BINARY_NAME openttd)
endif()

project(${BINARY_NAME})

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Please run \"cmake ..\" from the build directory. You may need to delete \"${CMAKE_SOURCE_DIR}/CMakeCache.txt\" first.")
endif()

# Debug mode by default.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if (EMSCRIPTEN)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/os/emscripten/cmake")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")
set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)

# Use GNUInstallDirs to allow customisation
# but set our own default data and bin dir
if(NOT CMAKE_INSTALL_DATADIR)
    set(CMAKE_INSTALL_DATADIR "share/games")
endif()
if(NOT CMAKE_INSTALL_BINDIR)
    set(CMAKE_INSTALL_BINDIR "games")
endif()
include(GNUInstallDirs)

include(Options)
set_options()
set_directory_options()

include(Static)
set_static_if_needed()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

set(CMAKE_EXPORT_COMPILE_COMMANDS YES)

# An empty target for the tools
add_custom_target(tools)

include(Endian)
add_endian_definition()

include(CompileFlags)
compile_flags()

if(APPLE OR UNIX)
    add_definitions(-DUNIX)
endif()

if(UNIX)
    find_package(Doxygen)
endif()

list(APPEND GENERATED_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/rev.cpp")
if(WIN32)
    list(APPEND GENERATED_SOURCE_FILES "${CMAKE_BINARY_DIR}/generated/ottdres.rc")
endif()

# Generate a target to determine version, which is execute every 'make' run
add_custom_target(find_version
        ${CMAKE_COMMAND}
                -DFIND_VERSION_BINARY_DIR=${CMAKE_BINARY_DIR}/generated
                -DCPACK_BINARY_DIR=${CMAKE_BINARY_DIR}
                -P "${CMAKE_SOURCE_DIR}/cmake/scripts/FindVersion.cmake"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        BYPRODUCTS ${GENERATED_SOURCE_FILES}
)

# Documentation
if(DOXYGEN_EXECUTABLE)
    add_custom_target(docs)
    add_custom_target(docs_source
        ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating documentation for source"
    )
    add_dependencies(docs_source
        find_version
    )
    add_dependencies(docs
        docs_source
    )
endif()

include(AddCustomXXXTimestamp)

if(OPTION_TOOLS_ONLY)
    if(HOST_BINARY_DIR)
        unset(HOST_BINARY_DIR CACHE)
    endif()
    add_subdirectory(${CMAKE_SOURCE_DIR}/src)
    return()
endif()

if(APPLE)
	# Avoid searching for headers in Frameworks, and libraries in LIBDIR.
	set(CMAKE_FIND_FRAMEWORK LAST)
endif()

# Prefer -pthread over -lpthread, which is often the better option of the two.
set(CMAKE_THREAD_PREFER_PTHREAD YES)
# Make sure we have Threads available.
find_package(Threads REQUIRED)

find_package(ZLIB)
find_package(LibLZMA)
find_package(LZO)
find_package(PNG)

if(NOT OPTION_DEDICATED)
    if(NOT WIN32)
        find_package(Allegro)
        if(NOT APPLE)
            find_package(Freetype)
            find_package(SDL2)
            if(NOT SDL2_FOUND)
                find_package(SDL)
            endif()
            find_package(Fluidsynth)
            find_package(Fontconfig)
            find_package(ICU OPTIONAL_COMPONENTS i18n lx)
        endif()
    endif()
endif()
if(APPLE)
    find_package(Iconv)

    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(COCOA_LIBRARY Cocoa)
    find_library(QUARTZCORE_LIBRARY QuartzCore)
endif()

if(NOT EMSCRIPTEN AND NOT OPTION_DEDICATED)
    find_package(OpenGL COMPONENTS OpenGL)
endif()

if(MSVC)
    find_package(Editbin REQUIRED)
endif()

find_package(SSE)
find_package(Xaudio2)

find_package(Grfcodec)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_FOUND)

show_options()

if(UNIX AND NOT APPLE AND NOT OPTION_DEDICATED)
    if(NOT SDL_FOUND AND NOT SDL2_FOUND AND NOT ALLEGRO_FOUND)
        message(FATAL_ERROR "SDL, SDL2 or Allegro is required for this platform")
    endif()
endif()
if(APPLE)
    if(NOT AUDIOTOOLBOX_LIBRARY)
        message(FATAL_ERROR "AudioToolbox is required for this platform")
    endif()
    if(NOT AUDIOUNIT_LIBRARY)
        message(FATAL_ERROR "AudioUnit is required for this platform")
    endif()
    if(NOT COCOA_LIBRARY)
        message(FATAL_ERROR "Cocoa is required for this platform")
    endif()
    if(NOT QUARTZCORE_LIBRARY)
        message(FATAL_ERROR "QuartzCore is required for this platform")
    endif()
endif()

if(OPTION_PACKAGE_DEPENDENCIES)
    if(NOT UNIX)
        message(FATAL_ERROR "Can only package dependencies on Linux")
    endif()
    if(OPTION_INSTALL_FHS)
        message(FATAL_ERROR "Cannot install in FHS folders when we are packaging dependencies")
    endif()
    if(${CMAKE_VERSION} VERSION_LESS "3.16.0")
        message(FATAL_ERROR "OPTION_PACKAGE_DEPENDENCIES can only work with CMake 3.16+; you are using ${CMAKE_VERSION}")
    endif()

    # If we are packaging dependencies, we do two things:
    # 1) set the RPATH to include $ORIGIN/lib; $ORIGIN (that literal string)
    #    is a Linux indicator for "path where application is". In CMake, we
    #    have to do this before add_executable() is executed.
    # 2) copy the libraries that we compile against to the "lib" folder.
    #    This is done in InstallAndPackage.cmake.
    set(CMAKE_INSTALL_RPATH "\$ORIGIN/lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

include(SourceList)

# Needed by rev.cpp
include_directories(${CMAKE_SOURCE_DIR}/src)
# Needed by everything that uses Squirrel
include_directories(${CMAKE_SOURCE_DIR}/src/3rdparty/squirrel/include)

include(MSVCFilters)

if(NOT TEST)
    add_executable(openttd WIN32 ${GENERATED_SOURCE_FILES})
else()
    add_executable(openttd ${GENERATED_SOURCE_FILES})
endif()

set_target_properties(openttd PROPERTIES OUTPUT_NAME "${BINARY_NAME}")
# All other files are added via target_sources()

if(MSVC)
    # Add DPI manifest to project; other WIN32 targets get this via ottdres.rc
    target_sources(openttd PRIVATE "${CMAKE_SOURCE_DIR}/os/windows/openttd.manifest")
endif()

add_subdirectory(${CMAKE_SOURCE_DIR}/bin)
add_subdirectory(${CMAKE_SOURCE_DIR}/src)
add_subdirectory(${CMAKE_SOURCE_DIR}/media)

add_dependencies(openttd
    find_version)

target_link_libraries(openttd
    openttd::languages
    openttd::settings
    openttd::media
    openttd::basesets
    openttd::script_api
    Threads::Threads
)

if(IPO_FOUND)
    set_target_properties(openttd PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE True)
    set_target_properties(openttd PROPERTIES INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL True)
    set_target_properties(openttd PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO True)
endif()
set_target_properties(openttd PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
process_compile_flags()

include(LinkPackage)
link_package(PNG TARGET PNG::PNG ENCOURAGED)
link_package(ZLIB TARGET ZLIB::ZLIB ENCOURAGED)
link_package(LIBLZMA TARGET LibLZMA::LibLZMA ENCOURAGED)
link_package(LZO)

if(NOT OPTION_DEDICATED)
    link_package(Fluidsynth)
    link_package(SDL)
    link_package(SDL2 TARGET SDL2::SDL2)
    link_package(Allegro)
    link_package(FREETYPE TARGET Freetype::Freetype)
    link_package(Fontconfig TARGET Fontconfig::Fontconfig)
    link_package(ICU_lx)
    link_package(ICU_i18n)

    if(SDL2_FOUND AND OPENGL_FOUND AND UNIX)
        # SDL2 dynamically loads OpenGL if needed, so do not link to OpenGL when
        # on Linux. For Windows, we need to link to OpenGL as we also have a win32
        # driver using it.
        add_definitions(-DWITH_OPENGL)
        message(STATUS "OpenGL found -- -DWITH_OPENGL -- (via SDL2)")
    else()
        link_package(OpenGL TARGET OpenGL::GL)
    endif()
endif()

if(APPLE)
    link_package(Iconv TARGET Iconv::Iconv)

    target_link_libraries(openttd
        ${AUDIOTOOLBOX_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${COCOA_LIBRARY}
        ${QUARTZCORE_LIBRARY}
    )

    add_definitions(
        -DWITH_COCOA
    )
endif()

if(EMSCRIPTEN)
    add_library(WASM::WASM INTERFACE IMPORTED)

    # Allow heap-growth, and start with a bigger memory size.
    target_link_libraries(WASM::WASM INTERFACE "-s ALLOW_MEMORY_GROWTH=1")
    target_link_libraries(WASM::WASM INTERFACE "-s INITIAL_MEMORY=33554432")
    target_link_libraries(WASM::WASM INTERFACE "-s DISABLE_EXCEPTION_CATCHING=0")
    add_definitions(-s DISABLE_EXCEPTION_CATCHING=0)

    # Export functions to Javascript.
    target_link_libraries(WASM::WASM INTERFACE "-s EXPORTED_FUNCTIONS='[\"_main\", \"_em_openttd_add_server\"]' -s EXTRA_EXPORTED_RUNTIME_METHODS='[\"cwrap\"]'")

    # Preload all the files we generate during build.
    # As we do not compile with FreeType / FontConfig, we also have no way to
    # render several languages (like Chinese, ..), so where do you draw the
    # line what languages to include and which not? In the end, especially as
    # the more languages you add the slower downloading becomes, we decided to
    # only ship the English language.
    target_link_libraries(WASM::WASM INTERFACE "--preload-file ${CMAKE_BINARY_DIR}/baseset@/baseset")
    target_link_libraries(WASM::WASM INTERFACE "--preload-file ${CMAKE_BINARY_DIR}/lang/english.lng@/lang/english.lng")
    target_link_libraries(WASM::WASM INTERFACE "--preload-file ${CMAKE_SOURCE_DIR}/bin/ai@/ai")
    target_link_libraries(WASM::WASM INTERFACE "--preload-file ${CMAKE_SOURCE_DIR}/bin/game@/game")

    # We use IDBFS for persistent storage.
    target_link_libraries(WASM::WASM INTERFACE "-lidbfs.js")

    # Use custom pre-js and shell.html.
    target_link_libraries(WASM::WASM INTERFACE "--pre-js ${CMAKE_SOURCE_DIR}/os/emscripten/pre.js")
    target_link_libraries(WASM::WASM INTERFACE "--shell-file ${CMAKE_SOURCE_DIR}/os/emscripten/shell.html")

    # Build the .html (which builds the .js, .wasm, and .data too).
    set_target_properties(openttd PROPERTIES SUFFIX ".html")
    target_link_libraries(openttd WASM::WASM)
endif()

if(NOT PERSONAL_DIR STREQUAL "(not set)")
    add_definitions(
        -DWITH_PERSONAL_DIR
        -DPERSONAL_DIR="${PERSONAL_DIR}"
    )
endif()

if(NOT SHARED_DIR STREQUAL "(not set)")
    add_definitions(
        -DWITH_SHARED_DIR
        -DSHARED_DIR="${SHARED_DIR}"
    )
endif()

if(NOT GLOBAL_DIR STREQUAL "(not set)")
    add_definitions(
        -DGLOBAL_DATA_DIR="${GLOBAL_DIR}"
    )
endif()

link_package(SSE)

add_definitions_based_on_options()

if(WIN32)
    add_definitions(
        -DUNICODE
        -D_UNICODE
        -DWITH_UNISCRIBE
    )

    target_link_libraries(openttd
        ws2_32
        winmm
        imm32
    )
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_definitions(-D_SQ64)
endif()

include(CreateRegression)
create_regression()

if(APPLE OR WIN32)
    find_package(Pandoc)
endif()

include(InstallAndPackage)
