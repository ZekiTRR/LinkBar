cmake_minimum_required(VERSION 3.20)

project(Linkbar VERSION 1.8.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /EHsc)
    add_compile_definitions(_WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000 WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra)
    add_compile_definitions(_WIN32_WINNT=0x0A00 NTDDI_VERSION=0x0A000000 WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

set(SOURCES
    src/main.cpp
    src/Constants.cpp
    src/StringUtil.cpp
    src/IniFile.cpp
    src/DibBitmap.cpp
    src/Os.cpp
    src/Common.cpp
    src/DarkTheme.cpp
    src/Localization.cpp
    src/Theme.cpp
    src/Hint.cpp
    src/DirectoryWatch.cpp
    src/Shell.cpp
    src/Items.cpp
    src/AppBar.cpp
    src/BarWindow.cpp
    src/Dialogs.cpp
    src/ColorPicker.cpp
    src/Jumplist.cpp
)

set(HEADERS
    src/Constants.h
    src/ComPtr.h
    src/ColorValue.h
    src/GdiplusCompat.h
    src/StringUtil.h
    src/IniFile.h
    src/DibBitmap.h
    src/Os.h
    src/Common.h
    src/DarkTheme.h
    src/Localization.h
    src/Theme.h
    src/Hint.h
    src/DirectoryWatch.h
    src/Shell.h
    src/Items.h
    src/AppBar.h
    src/BarWindow.h
    src/Dialogs.h
    src/ColorPicker.h
    src/Jumplist.h
)

add_executable(linkbar WIN32 ${SOURCES} ${HEADERS} resource.rc)

target_include_directories(linkbar PRIVATE src)

target_link_libraries(linkbar PRIVATE
    gdiplus comctl32 shlwapi shell32 ole32 oleaut32 uuid dwmapi uxtheme version msimg32 advapi32
)

set_target_properties(linkbar PROPERTIES
    OUTPUT_NAME "linkbar"
    LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    VS_GLOBAL_GenerateManifest FALSE
)

# Smallest Release binary: whole-program optimization, COMDAT folding and a
# stripped static CRT (~150 KB instead of ~550 KB).
if(MSVC)
    target_compile_options(linkbar PRIVATE "$<$<CONFIG:Release>:/Gy;/Gw;/GL>")
    target_link_options(linkbar PRIVATE
        "$<$<CONFIG:Release>:/LTCG;/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>"
    )
endif()

target_link_options(linkbar PRIVATE
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/linkbar.manifest"
)

# The application reads Locales\<lang>.ini next to the executable.
add_custom_command(TARGET linkbar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Locales"
        "$<TARGET_FILE_DIR:linkbar>/Locales"
)

