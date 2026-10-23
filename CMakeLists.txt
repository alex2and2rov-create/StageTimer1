cmake_minimum_required(VERSION 3.20)
project(StageTimer VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Stage Timer — FFGL source plugin for Resolume Arena / Avenue 7.x
#
#   Windows : cmake -B build -A x64     && cmake --build build --config Release
#   macOS   : cmake -B build -G Xcode   && cmake --build build --config Release
#
# Result: build/dist/StageTimer.dll (Windows) or build/dist/StageTimer.bundle (macOS).
# No runtime dependencies: fonts are embedded. FFGL SDK and GLEW are downloaded automatically.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "" FORCE)
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8 /bigobj)
endif()

include(FetchContent)

set(FFGL_SDK_DIR "" CACHE PATH "Path to a checkout of github.com/resolume/ffgl (downloaded if empty)")
if(NOT FFGL_SDK_DIR)
    FetchContent_Declare(ffgl GIT_REPOSITORY https://github.com/resolume/ffgl.git GIT_TAG master GIT_SHALLOW TRUE
        SOURCE_SUBDIR _populate_only_)
    FetchContent_MakeAvailable(ffgl)
    set(FFGL_SDK_DIR ${ffgl_SOURCE_DIR})
endif()
set(FFGL_LIB ${FFGL_SDK_DIR}/source/lib)

if(NOT APPLE)
    set(GLEW_DIR "" CACHE PATH "Path to an extracted glew-2.2.0 source release (downloaded if empty)")
    if(NOT GLEW_DIR)
        FetchContent_Declare(glew URL https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.tgz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR _populate_only_)
        FetchContent_MakeAvailable(glew)
        set(GLEW_DIR ${glew_SOURCE_DIR})
    endif()
endif()

# Sources may live in src/ (normal layout) or directly next to this file
# (when uploaded through a browser that drops folders).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/FFGLTimer.cpp)
    set(SRC src)
else()
    set(SRC .)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/timer_tests.cpp)
    set(TESTS_DIR tests)
else()
    set(TESTS_DIR .)
endif()
message(STATUS "Stage Timer sources: ${SRC}")

set(SOURCES
    ${SRC}/FFGLTimer.cpp
    ${SRC}/FFGLTimer.h
    ${SRC}/TimerLogic.cpp
    ${SRC}/TimerLogic.h
    ${SRC}/TextRasterizer.cpp
    ${SRC}/TextRasterizer.h
    ${FFGL_LIB}/FFGLSDK.cpp
)
if(NOT APPLE)
    list(APPEND SOURCES ${GLEW_DIR}/src/glew.c)
endif()
if(WIN32)
    file(WRITE ${CMAKE_BINARY_DIR}/StageTimer.def "LIBRARY StageTimer\nEXPORTS\n\tplugMain\n")
    list(APPEND SOURCES ${CMAKE_BINARY_DIR}/StageTimer.def)
endif()

if(APPLE)
    add_library(StageTimer MODULE ${SOURCES})
    set_target_properties(StageTimer PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION bundle
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME StageTimer
        MACOSX_BUNDLE_GUI_IDENTIFIER com.stagetimer.ffgl
        XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH NO)
    target_link_libraries(StageTimer PRIVATE "-framework OpenGL" "-framework Cocoa")
else()
    add_library(StageTimer SHARED ${SOURCES})
    set_target_properties(StageTimer PROPERTIES PREFIX "")
    target_compile_definitions(StageTimer PRIVATE GLEW_STATIC GLEW_NO_GLU)
    target_include_directories(StageTimer PRIVATE ${GLEW_DIR}/include)
endif()
target_include_directories(StageTimer PRIVATE ${FFGL_LIB} ${SRC} ${SRC}/fonts third_party)

if(WIN32)
    target_link_libraries(StageTimer PRIVATE opengl32)
    target_compile_definitions(StageTimer PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    if(MINGW)
        target_link_options(StageTimer PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
elseif(NOT APPLE)
    # Linux: test builds only (Resolume has no Linux version)
    target_link_libraries(StageTimer PRIVATE dl pthread)
endif()

set(DIST_DIR ${CMAKE_BINARY_DIR}/dist)
if(WIN32)
    add_custom_command(TARGET StageTimer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:StageTimer> ${DIST_DIR}/StageTimer.dll)
elseif(APPLE)
    add_custom_command(TARGET StageTimer POST_BUILD
        COMMAND codesign --force --sign - $<TARGET_BUNDLE_DIR:StageTimer>
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DIST_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory $<TARGET_BUNDLE_DIR:StageTimer> ${DIST_DIR}/StageTimer.bundle)
endif()

option(STAGETIMER_BUILD_TESTS "Build unit tests for the timer logic" OFF)
if(STAGETIMER_BUILD_TESTS)
    add_executable(timer_tests ${TESTS_DIR}/timer_tests.cpp ${SRC}/TimerLogic.cpp)
    target_include_directories(timer_tests PRIVATE ${SRC})
endif()
