cmake_minimum_required(VERSION 3.10)

project(WrathClassicAPI LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# minhook's CMakeLists.txt declares `cmake_minimum_required(VERSION 3.0...3.5)`,
# which CMake 4.x flags as a deprecation warning every reconfigure. Bumping
# the minimum policy version here promotes its policy set to 3.10 without
# patching the vendored submodule.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
add_subdirectory(external/minhook)

# The loader-agnostic core (sources, generated addon header, tinycbor, version
# define) is defined once in cmake/WrathClassicAPICore.cmake and shared with the
# WXL-extension front-end in the wxl-wrathclassicapi repository. This repository
# supplies only the LichLoader front-end entry unit (src/DllMain.cpp) and the
# MinHook-backed IHookHost it installs the core's hooks through.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/WrathClassicAPICore.cmake)

add_library(${PROJECT_NAME} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/DllMain.cpp)
wrathclassicapi_add_core(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PRIVATE minhook)
target_include_directories(${PROJECT_NAME} PRIVATE external/minhook/include)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "WrathClassicAPI")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MT")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
# /DEF exports `Load` (LichCore's post-injection entry point) undecorated so
# its GetProcAddress(module, "Load") resolves. See src/WrathClassicAPI.def and
# the loader-lock rationale in src/DllMain.cpp.
set(CMAKE_SHARED_LINKER_FLAGS
    "/OPT:REF /OPT:ICF /MERGE:.rdata=.text /MERGE:.pdata=.text /SECTION:.text,EWR /NODEFAULTLIB:MSVCRT /DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/WrathClassicAPI.def"
)
