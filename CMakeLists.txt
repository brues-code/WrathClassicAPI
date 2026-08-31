cmake_minimum_required(VERSION 3.10)

project(WrathClassicAPI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# minhook's CMakeLists.txt declares `cmake_minimum_required(VERSION 3.0...3.5)`,
# which CMake 4.x flags as a deprecation warning every reconfigure. Bumping
# the minimum policy version here promotes its policy set to 3.10 without
# patching the vendored submodule.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
add_subdirectory(external/minhook)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h")

# tinycbor (vendored, external/tinycbor) — the C sources for C_EncodingUtil's
# CBOR serializer. Compiled straight into the DLL. picojson (external/picojson)
# is header-only, so C_EncodingUtil's JSON serializer needs no sources here.
set(TINYCBOR_SOURCES
    external/tinycbor/src/cborencoder.c
    external/tinycbor/src/cborencoder_close_container_checked.c
    external/tinycbor/src/cborencoder_float.c
    external/tinycbor/src/cborparser.c
    external/tinycbor/src/cborparser_dup_string.c
    external/tinycbor/src/cborparser_float.c
    external/tinycbor/src/cborerrorstrings.c)

# Version derivation. Dev builds use the "DEV" sentinel, which the embedded
# addon's version-precedence check treats as OLDER than every real release —
# so a locally-built DLL never shadows an on-disk working copy. CI release
# builds pass `-DWRATHCLASSICAPI_TAG=vX.Y.Z`, which drives BOTH the compiled
# `WRATH_CLASSIC_API_VERSION` int AND the embedded addon's `## Version:` toc
# line (stamped in-memory by embed_addon.cmake). The on-disk source toc stays
# `## Version: DEV`; only the embedded copy is stamped, so a release build
# needs nothing but the flag — no source edit.
set(WRATHCLASSICAPI_VERSION_VALUE 1)
set(WRATHCLASSICAPI_TOC_VERSION "DEV")

set(WRATHCLASSICAPI_TAG
    ""
    CACHE STRING "Release tag like v1.2.3")

if(WRATHCLASSICAPI_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(_MAJOR "${CMAKE_MATCH_1}")
  set(_MINOR "${CMAKE_MATCH_2}")
  set(_PATCH "${CMAKE_MATCH_3}")
  math(EXPR WRATHCLASSICAPI_VERSION_VALUE
       "${_MAJOR} * 10000 + ${_MINOR} * 100 + ${_PATCH}")
  set(WRATHCLASSICAPI_TOC_VERSION "${_MAJOR}.${_MINOR}.${_PATCH}")
elseif(WRATHCLASSICAPI_TAG)
  message(
    FATAL_ERROR
      "Invalid WRATHCLASSICAPI_TAG: ${WRATHCLASSICAPI_TAG} (expected vMAJOR.MINOR.PATCH)"
  )
endif()

# Bundle `AddOns/!!!WrathClassicAPI/` into a generated header so the DLL can
# surface the addon to the engine even when the user doesn't have it installed
# on disk. The file-read hook in `src/addons/Embedded.cpp` serves these buffers
# for paths under `Interface\AddOns\!!!WrathClassicAPI\`; a newer on-disk copy
# (or a `.wrathclassicapi-dev` marker) wins, otherwise the embedded copy does.
# `ADDON_VERSION` stamps the embedded `.toc`'s `## Version:` line for releases.
set(EMBEDDED_ADDON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/AddOns/!!!WrathClassicAPI")
set(EMBEDDED_ADDON_HEADER "${CMAKE_BINARY_DIR}/generated/embedded_wrathclassicapi.h")
file(GLOB_RECURSE EMBEDDED_ADDON_FILES CONFIGURE_DEPENDS "${EMBEDDED_ADDON_SRC}/*")
add_custom_command(
    OUTPUT "${EMBEDDED_ADDON_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${EMBEDDED_ADDON_SRC}
        -DOUT_HEADER=${EMBEDDED_ADDON_HEADER}
        -DADDON_NAME=WrathClassicAPI
        -DADDON_VERSION=${WRATHCLASSICAPI_TOC_VERSION}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    DEPENDS
        ${EMBEDDED_ADDON_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    COMMENT "Embedding !!!WrathClassicAPI addon into ${EMBEDDED_ADDON_HEADER}"
    VERBATIM)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${TINYCBOR_SOURCES} ${EMBEDDED_ADDON_HEADER})

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE WRATHCLASSICAPI_VERSION_VALUE=${WRATHCLASSICAPI_VERSION_VALUE})

target_link_libraries(${PROJECT_NAME} PRIVATE minhook)

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          ${CMAKE_BINARY_DIR}/generated
                          external/minhook/include
                          external
                          external/tinycbor/src)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "WrathClassicAPI")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
# /DEF exports `Load` (LichCore's post-injection entry point) undecorated so
# its GetProcAddress(module, "Load") resolves. See src/WrathClassicAPI.def and
# the loader-lock rationale in src/DllMain.cpp.
set(CMAKE_SHARED_LINKER_FLAGS
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/WrathClassicAPI.def"
)
