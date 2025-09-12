cmake_minimum_required(VERSION 3.20)

project(MyProject LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# enable visual studio hot reload
if (MSVC AND WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)

    # option 1: put the following command before the creation of the first
    #           compiled target you want to apply the option to;
    #           this applies to targets created in this dir and subdirectories
    #add_link_options($<$<CONFIG:Debug>:/INCREMENTAL>)
    add_compile_options($<$<CONFIG:Debug>:/ZI>)

    add_link_options(
    /LARGEADDRESSAWARE
    /DEBUG
    $<$<NOT:$<CONFIG:DEBUG>>:/INCREMENTAL:NO> # Disable incremental linking.
    $<$<NOT:$<CONFIG:DEBUG>>:/OPT:REF> # Remove unreferenced functions and data.
    $<$<NOT:$<CONFIG:DEBUG>>:/OPT:ICF> # Identical COMDAT folding.
    $<$<CONFIG:DEBUG>:/INCREMENTAL> # Do incremental linking.
    $<$<CONFIG:DEBUG>:/OPT:NOREF> # No unreferenced data elimination.
    $<$<CONFIG:DEBUG>:/OPT:NOICF> # No Identical COMDAT folding.
)

    # option 2: apply the option on a per-target basis
    #target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
    #target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:/ZI>)
endif()




# Automatically find all source files in the src directory
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Check if any source files were found
if(NOT SOURCES)
    message(FATAL_ERROR "No source files found in the 'src' directory. Please add at least one .cpp file (e.g., src/main.cpp) to your project.")
endif()

# Include FetchContent to manage external libraries
include(FetchContent)

# --- Dependency: tinyfiledialogs ---
FetchContent_Declare(
  tinyfiledialogs_source
  GIT_REPOSITORY https://github.com/native-toolkit/tinyfiledialogs.git
  GIT_TAG        master
)
FetchContent_GetProperties(tinyfiledialogs_source)
if(NOT tinyfiledialogs_source_POPULATED)
  FetchContent_Populate(tinyfiledialogs_source)
endif()

# Add the tinyfiledialogs source file directly to your project's sources
list(APPEND SOURCES ${tinyfiledialogs_source_SOURCE_DIR}/tinyfiledialogs.c)

# --- Dependency: raygui (Header-Only with implementation) ---
FetchContent_Declare(
  raygui_source
  GIT_REPOSITORY https://github.com/raysan5/raygui.git
  GIT_TAG        master # Consider using a specific tag for stability
)
FetchContent_GetProperties(raygui_source)
if(NOT raygui_source_POPULATED)
  FetchContent_Populate(raygui_source)
endif()

# Create a file to contain the raygui implementation
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/raygui_impl.cpp "#define RAYGUI_IMPLEMENTATION\n#include \"raygui.h\"\n")
list(APPEND SOURCES ${CMAKE_CURRENT_BINARY_DIR}/raygui_impl.cpp)

# Create the executable for your project
add_executable(MyProject ${SOURCES})

# Add src and other include paths to the project
target_include_directories(MyProject PRIVATE 
    src
    ${tinyfiledialogs_source_SOURCE_DIR}
    ${raygui_source_SOURCE_DIR}/src
)

# --- Dependency: flecs ---
FetchContent_Declare(
  flecs
  GIT_REPOSITORY https://github.com/SanderMertens/flecs.git
  GIT_TAG        master # Consider using a specific tag for stability
)
FetchContent_MakeAvailable(flecs)

# --- Dependency: raylib ---
# Disable building examples and games for raylib to speed up configuration
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG        master # Consider using a specific tag for stability
)
FetchContent_MakeAvailable(raylib)

# --- Dependency: Chipmunk2D ---
# Note: Chipmunk2D is currently disabled. Uncomment to use.
#FetchContent_Declare(
#  Chipmunk2D
#  GIT_REPOSITORY https://github.com/slembcke/Chipmunk2D.git
#  GIT_TAG        master # Consider using a specific tag for stability
#)
#FetchContent_MakeAvailable(Chipmunk2D)

# --- Dependency: nuklear (Header-Only) ---
#FetchContent_Declare(
#  nuklear_source
#  GIT_REPOSITORY https://github.com/vurtun/nuklear.git
#  GIT_TAG        master
#)
#FetchContent_MakeAvailable(nuklear_source)

# Add an INTERFACE library for nuklear to provide its include directory
#add_library(nuklear INTERFACE)
#target_include_directories(nuklear INTERFACE ${nuklear_source_SOURCE_DIR})

# --- Dependency: cJSON ---
# Disable building tests for cJSON
#set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
#FetchContent_Declare(
#  cJSON
#  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
#  GIT_TAG        master # Consider using a specific tag for stability
#)
#FetchContent_MakeAvailable(cJSON)

# Link all the fetched libraries to your executable
target_link_libraries(MyProject PRIVATE
    flecs
    raylib
    #chipmunk # This is commented out as Chipmunk2D is disabled above
    #nuklear
    #cjson
)

# Build dependencies as static libraries
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build dependencies as static libraries")