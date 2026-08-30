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

# Bundle `AddOns/!!!WrathClassicAPI/` into a generated header so the DLL can
# surface the addon to the engine even when the user doesn't have it installed
# on disk. The file-read hook in `src/addons/Embedded.cpp` returns these
# buffers when the engine asks for paths under
# `Interface\AddOns\!!!WrathClassicAPI\` (disk copy wins when present).
set(EMBEDDED_ADDON_SRC "${CMAKE_CURRENT_SOURCE_DIR}/AddOns/!!!WrathClassicAPI")
set(EMBEDDED_ADDON_HEADER "${CMAKE_BINARY_DIR}/generated/embedded_wrathclassicapi.h")
file(GLOB_RECURSE EMBEDDED_ADDON_FILES CONFIGURE_DEPENDS "${EMBEDDED_ADDON_SRC}/*")
add_custom_command(
    OUTPUT "${EMBEDDED_ADDON_HEADER}"
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR=${EMBEDDED_ADDON_SRC}
        -DOUT_HEADER=${EMBEDDED_ADDON_HEADER}
        -DADDON_NAME=WrathClassicAPI
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    DEPENDS
        ${EMBEDDED_ADDON_FILES}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_addon.cmake"
    COMMENT "Embedding !!!WrathClassicAPI addon into ${EMBEDDED_ADDON_HEADER}"
    VERBATIM)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${EMBEDDED_ADDON_HEADER})

set(WRATHCLASSICAPI_VERSION_VALUE 1)

set(WRATHCLASSICAPI_TAG
    ""
    CACHE STRING "Release tag like v1.2.3")

if(WRATHCLASSICAPI_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  set(_MAJOR "${CMAKE_MATCH_1}")
  set(_MINOR "${CMAKE_MATCH_2}")
  set(_PATCH "${CMAKE_MATCH_3}")
  math(EXPR WRATHCLASSICAPI_VERSION_VALUE
       "${_MAJOR} * 10000 + ${_MINOR} * 100 + ${_PATCH}")
elseif(WRATHCLASSICAPI_TAG)
  message(
    FATAL_ERROR
      "Invalid WRATHCLASSICAPI_TAG: ${WRATHCLASSICAPI_TAG} (expected vMAJOR.MINOR.PATCH)"
  )
endif()

target_compile_definitions(
  ${PROJECT_NAME}
  PRIVATE WRATHCLASSICAPI_VERSION_VALUE=${WRATHCLASSICAPI_VERSION_VALUE})

target_link_libraries(${PROJECT_NAME} PRIVATE minhook)

target_include_directories(
  ${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
                          ${CMAKE_BINARY_DIR}/generated
                          external/minhook/include
                          external)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "WrathClassicAPI")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
# /DEF exports `Load` (LichCore's post-injection entry point) undecorated so
# its GetProcAddress(module, "Load") resolves. See src/WrathClassicAPI.def and
# the loader-lock rationale in src/DllMain.cpp.
set(CMAKE_SHARED_LINKER_FLAGS
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/WrathClassicAPI.def"
)
