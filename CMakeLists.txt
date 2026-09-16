cmake_minimum_required(VERSION 3.12)

file(READ "vcpkg.json" VCPKG_JSON_CONTENT)
string(JSON VCPKG_PROJECT_VERSION GET "${VCPKG_JSON_CONTENT}" "version")

project(TBH_Cheetos VERSION ${VCPKG_PROJECT_VERSION} LANGUAGES CXX C RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -s")

# Default to Release build for -O3 optimizations (crucial for scanner speed)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Executable name
set(EXE_NAME "TBH-Cheetos")

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.c"
)
# Exclude speedhack.cpp — it's compiled as a separate DLL for injection
list(FILTER ALL_SOURCES EXCLUDE REGEX "speedhack\\.cpp$")

# Add executable
add_executable(${EXE_NAME}
    ${ALL_SOURCES}
    resources/resources.rc
)

if(NOT APP_VERSION)
    set(APP_VERSION "v${PROJECT_VERSION}")
endif()

target_include_directories(${EXE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_options(${EXE_NAME} PRIVATE "-mwindows")
target_compile_definitions(
    ${EXE_NAME} PRIVATE APP_VERSION="${APP_VERSION}"
    NDEBUG
)

# UI Dependencies
find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(${EXE_NAME} PRIVATE imgui::imgui glfw opengl32)

# Speedhack DLL target (injected into game process, must be separate)
find_package(minhook CONFIG REQUIRED)

add_library(speedhack SHARED
    src/features/speedhack.cpp
)
target_link_libraries(speedhack PRIVATE minhook::minhook)
set_target_properties(speedhack PROPERTIES
    PREFIX ""
    # Don't generate .dll.a import library — we don't link against it
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/implib"
)

# Platform specific configurations
if(MSVC)
    # Windows native MSVC
    target_compile_options(${EXE_NAME} PRIVATE /EHsc)
    target_link_libraries(${EXE_NAME} PRIVATE comctl32 psapi)
elseif(MINGW)
    # MinGW (Cross-compiled on Linux, or native MinGW on Windows)
    # Statically link libgcc and libstdc++ so it runs standalone on Windows/Wine
    target_link_options(${EXE_NAME} PRIVATE -static)
    target_link_libraries(${EXE_NAME} PRIVATE comctl32 psapi)
endif()

# Generate README from template
configure_file(
    "${CMAKE_SOURCE_DIR}/resources/templates/README.txt.in"
    "${CMAKE_BINARY_DIR}/README.txt"
    @ONLY
)

# --- CPack Configuration for Release Packaging ---
include(InstallRequiredSystemLibraries)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/license")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_FILE_NAME "TBH-Cheetos-v${PROJECT_VERSION}")
set(CPACK_GENERATOR "ZIP")

# Install instructions (CPack uses these to know what to zip)
install(TARGETS ${EXE_NAME} speedhack RUNTIME DESTINATION .)
install(FILES
    "${CMAKE_SOURCE_DIR}/scripts/launch.bat"
    "${CMAKE_BINARY_DIR}/README.txt"
    DESTINATION .
)
install(PROGRAMS
    "${CMAKE_SOURCE_DIR}/scripts/launch_linux.sh"
    DESTINATION .
)

include(CPack)
