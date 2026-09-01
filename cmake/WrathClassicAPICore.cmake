# Shared build recipe for the loader-agnostic WrathClassicAPI core.
#
# The "core" is every src/*.cpp EXCEPT the front-end entry translation unit
# (src/DllMain.cpp, the LichLoader-injected DLL's entry point). Both front-ends
# build from the same core: the LichLoader DLL in this repository, and the WXL
# extension in wxl-wrathclassicapi. Keeping the source list, generated inputs,
# include directories and version define here means the core build recipe lives
# in exactly one place; a front-end adds only its own entry unit.
#
# Usage from a front-end CMakeLists.txt:
#   include(<this-repo>/cmake/WrathClassicAPICore.cmake)
#   add_library(myfrontend SHARED <the front-end's own entry sources>)
#   wrathclassicapi_add_core(myfrontend)
# The caller links whatever its loader needs (e.g. MinHook); the core itself
# references no hook engine — it installs through Game::IHookHost.

# This repository's root, derived from THIS file's location, so that including
# the fragment from a sibling repository still resolves the core sources here
# rather than in the caller's own tree.
get_filename_component(WRATHCLASSICAPI_CORE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# --- version derivation ---------------------------------------------------------------------------
# Dev builds use the "DEV" sentinel, which the embedded addon's version-precedence
# check treats as OLDER than every real release — so a locally-built DLL never
# shadows an on-disk working copy. CI release builds pass -DWRATHCLASSICAPI_TAG=vX.Y.Z,
# which drives BOTH the compiled WRATH_CLASSIC_API_VERSION int AND the embedded
# addon's `## Version:` toc line (stamped in-memory by embed_addon.cmake).
set(WRATHCLASSICAPI_VERSION_VALUE 1)
set(WRATHCLASSICAPI_TOC_VERSION "DEV")
set(WRATHCLASSICAPI_TAG "" CACHE STRING "Release tag like v1.2.3")

if(WRATHCLASSICAPI_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(_MAJOR "${CMAKE_MATCH_1}")
  set(_MINOR "${CMAKE_MATCH_2}")
  set(_PATCH "${CMAKE_MATCH_3}")
  math(EXPR WRATHCLASSICAPI_VERSION_VALUE
       "${_MAJOR} * 10000 + ${_MINOR} * 100 + ${_PATCH}")
  set(WRATHCLASSICAPI_TOC_VERSION "${_MAJOR}.${_MINOR}.${_PATCH}")
elseif(WRATHCLASSICAPI_TAG)
  message(FATAL_ERROR
    "Invalid WRATHCLASSICAPI_TAG: ${WRATHCLASSICAPI_TAG} (expected vMAJOR.MINOR.PATCH)")
endif()

# tinycbor (vendored, external/tinycbor) — the C sources for C_EncodingUtil's
# CBOR serializer, compiled straight into the front-end DLL. picojson
# (external/picojson) is header-only, so the JSON serializer needs no sources.
set(WRATHCLASSICAPI_TINYCBOR_SOURCES
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborencoder.c"
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborencoder_close_container_checked.c"
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborencoder_float.c"
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborparser.c"
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborparser_dup_string.c"
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborparser_float.c"
    "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src/cborerrorstrings.c")

# Adds the core's sources, generated inputs, include directories and version
# define to an already-created front-end target.
function(wrathclassicapi_add_core target)
    # Core sources: every src/*.cpp except the front-end entry unit. Filter by
    # filename (not a full-path REMOVE_ITEM) so path casing/separators from the
    # glob can't cause DllMain.cpp to slip back into a sibling repo's build.
    file(GLOB_RECURSE _core_sources CONFIGURE_DEPENDS "${WRATHCLASSICAPI_CORE_ROOT}/src/*.cpp")
    file(GLOB_RECURSE _core_headers CONFIGURE_DEPENDS "${WRATHCLASSICAPI_CORE_ROOT}/src/*.h")
    list(FILTER _core_sources EXCLUDE REGEX "/DllMain\\.cpp$")

    # Bundle AddOns/!!!WrathClassicAPI/ into a generated header so the DLL can
    # surface the addon to the engine even without an on-disk copy. Generated
    # into the caller's own binary dir.
    set(_embedded_src "${WRATHCLASSICAPI_CORE_ROOT}/AddOns/!!!WrathClassicAPI")
    set(_embedded_header "${CMAKE_BINARY_DIR}/generated/embedded_wrathclassicapi.h")
    file(GLOB_RECURSE _embedded_files CONFIGURE_DEPENDS "${_embedded_src}/*")
    add_custom_command(
        OUTPUT "${_embedded_header}"
        COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${_embedded_src}
            -DOUT_HEADER=${_embedded_header}
            -DADDON_NAME=WrathClassicAPI
            -DADDON_VERSION=${WRATHCLASSICAPI_TOC_VERSION}
            -P "${WRATHCLASSICAPI_CORE_ROOT}/cmake/embed_addon.cmake"
        DEPENDS
            ${_embedded_files}
            "${WRATHCLASSICAPI_CORE_ROOT}/cmake/embed_addon.cmake"
        COMMENT "Embedding !!!WrathClassicAPI addon into ${_embedded_header}"
        VERBATIM)

    target_sources(${target} PRIVATE
        ${_core_sources}
        ${_core_headers}
        ${WRATHCLASSICAPI_TINYCBOR_SOURCES}
        "${_embedded_header}")

    target_include_directories(${target} PRIVATE
        "${WRATHCLASSICAPI_CORE_ROOT}/src"
        "${CMAKE_BINARY_DIR}/generated"
        "${WRATHCLASSICAPI_CORE_ROOT}/external"
        "${WRATHCLASSICAPI_CORE_ROOT}/external/tinycbor/src")

    target_compile_definitions(${target} PRIVATE
        WRATHCLASSICAPI_VERSION_VALUE=${WRATHCLASSICAPI_VERSION_VALUE})
endfunction()
