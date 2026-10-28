cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config.json" CONFIG_JSON_CONTENT)
string(JSON PARSED_PROJECT_NAME GET "${CONFIG_JSON_CONTENT}" "project")
string(JSON PARSED_VERSION GET "${CONFIG_JSON_CONTENT}" "cheetos-version")

project(${PARSED_PROJECT_NAME} VERSION ${PARSED_VERSION} LANGUAGES CXX C RC)

if(NOT WIN32)
  message(FATAL_ERROR "Target system is not Windows! This project requires Windows binaries (.dll and .exe). "
                      "If you are compiling from Linux or macOS, please specify a cross-compiler toolchain "
                      "(e.g., -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Release build for -O3 optimizations (crucial for scanner speed)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -s")
set(CMAKE_C_FLAGS_RELEASE "-O3 -s")

# ============================================
# Vendor Dependencies (MinHook, GLFW, ImGui)
# ============================================

# 1. MinHook
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(MINHOOK_HDE_SRC "src/vendor/minhook/src/hde/hde64.c")
else()
  set(MINHOOK_HDE_SRC "src/vendor/minhook/src/hde/hde32.c")
endif()

add_library(minhook STATIC
  src/vendor/minhook/src/buffer.c
  src/vendor/minhook/src/hook.c
  src/vendor/minhook/src/trampoline.c
  ${MINHOOK_HDE_SRC}
)
target_include_directories(minhook PUBLIC
  src/vendor/minhook/include
)
target_include_directories(minhook PRIVATE
  src/vendor/minhook/src
  src/vendor/minhook/src/hde
)

# 2. GLFW (Core Win32 Static Library)
add_library(glfw STATIC
  src/vendor/glfw/src/context.c
  src/vendor/glfw/src/init.c
  src/vendor/glfw/src/input.c
  src/vendor/glfw/src/monitor.c
  src/vendor/glfw/src/platform.c
  src/vendor/glfw/src/vulkan.c
  src/vendor/glfw/src/window.c
  src/vendor/glfw/src/egl_context.c
  src/vendor/glfw/src/osmesa_context.c
  src/vendor/glfw/src/null_init.c
  src/vendor/glfw/src/null_monitor.c
  src/vendor/glfw/src/null_window.c
  src/vendor/glfw/src/null_joystick.c
  src/vendor/glfw/src/win32_init.c
  src/vendor/glfw/src/win32_joystick.c
  src/vendor/glfw/src/win32_monitor.c
  src/vendor/glfw/src/win32_window.c
  src/vendor/glfw/src/win32_time.c
  src/vendor/glfw/src/win32_thread.c
  src/vendor/glfw/src/win32_module.c
  src/vendor/glfw/src/wgl_context.c
)
target_compile_definitions(glfw PRIVATE _GLFW_WIN32)
target_include_directories(glfw PUBLIC
  src/vendor/glfw/include
)
target_include_directories(glfw PRIVATE
  src/vendor/glfw/src
)

# 3. ImGui (with GLFW + OpenGL3 backends)
add_library(imgui STATIC
  src/vendor/imgui/imgui.cpp
  src/vendor/imgui/imgui_draw.cpp
  src/vendor/imgui/imgui_tables.cpp
  src/vendor/imgui/imgui_widgets.cpp
  src/vendor/imgui/backends/imgui_impl_glfw.cpp
  src/vendor/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
  src/vendor/imgui
  src/vendor/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw opengl32)

# ============================================
# Cheetos Executable (TBH-Cheetos)
# ============================================
set(EXE_NAME "TBH-Cheetos")

file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.c"
)
# Exclude vendor directory and speedhack.cpp
list(FILTER ALL_SOURCES EXCLUDE REGEX "(/src/vendor/|speedhack\\.cpp$)")

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

target_link_libraries(${EXE_NAME} PRIVATE imgui glfw opengl32)

# ============================================
# Speedhack Injected DLL
# ============================================
add_library(speedhack SHARED
    src/features/speedhack.cpp
)
target_include_directories(speedhack PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(speedhack PRIVATE minhook)
set_target_properties(speedhack PROPERTIES
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/implib"
)

# Platform specific configurations
if(MSVC)
    target_compile_options(${EXE_NAME} PRIVATE /EHsc)
    target_link_libraries(${EXE_NAME} PRIVATE comctl32 psapi)
elseif(MINGW)
    target_link_options(${EXE_NAME} PRIVATE -static)
    target_link_options(speedhack PRIVATE -static)
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
