cmake_minimum_required(VERSION 3.21)

project(CorbataPDF VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Pdf PrintSupport Test)
find_package(qpdf CONFIG REQUIRED)

qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

# --- PDF assembly, Fill & Sign marks and form fields (shared by the app and the tests)
add_library(corbata_core STATIC
    src/PdfWriter.cpp src/PdfWriter.h src/PageRef.h
    src/Marks.cpp src/Marks.h
    src/FormFields.cpp src/FormFields.h
)
target_include_directories(corbata_core PUBLIC src)
target_link_libraries(corbata_core PUBLIC Qt6::Core Qt6::Gui qpdf::libqpdf)
# vcpkg's static qpdf names its zlib/libjpeg dependencies without paths.
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    target_link_directories(corbata_core PUBLIC
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/lib")
endif()

# --- Windows and widgets (shared by the app and the UI test)
add_library(corbata_ui STATIC
    src/PageGrid.cpp src/PageGrid.h
    src/PageModel.cpp src/PageModel.h
    src/Library.cpp src/Library.h
    src/DocumentView.cpp src/DocumentView.h
    src/SignatureDialog.cpp src/SignatureDialog.h
    src/Printing.cpp src/Printing.h
)
target_link_libraries(corbata_ui PUBLIC corbata_core Qt6::Widgets Qt6::Pdf Qt6::PrintSupport)
if(WIN32)
    # The Windows print dialog with live preview (Windows.Graphics.Printing, C++/WinRT).
    target_sources(corbata_ui PRIVATE src/WinPrint.cpp src/WinPrint.h)
    target_link_libraries(corbata_ui PUBLIC windowsapp d3d11 d2d1 dxgi windowscodecs ole32)
endif()

# --- The app
set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/DefaultApp.cpp src/DefaultApp.h
)

if(WIN32)
    list(APPEND APP_SOURCES src/DefaultApp_win.cpp)
    if(EXISTS ${CMAKE_SOURCE_DIR}/resources/app.ico)
        list(APPEND APP_SOURCES resources/app.rc)
    endif()
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND APP_SOURCES src/DefaultApp_mac.mm)
    set(MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set(APP_ICNS ${CMAKE_SOURCE_DIR}/resources/AppIcon.icns)
    if(EXISTS ${APP_ICNS})
        set_source_files_properties(${APP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        list(APPEND APP_SOURCES ${APP_ICNS})
    endif()
endif()

qt_add_executable(CorbataPDF WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_compile_definitions(CorbataPDF PRIVATE CORBATA_VERSION="${PROJECT_VERSION}")
target_link_libraries(CorbataPDF PRIVATE corbata_ui)
if(WIN32)
    target_link_libraries(CorbataPDF PRIVATE shlwapi shell32)
elseif(APPLE)
    target_link_libraries(CorbataPDF PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers")
endif()
# The window icon (the same picture as the .ico / .icns).
qt_add_resources(CorbataPDF "icon" PREFIX "/" BASE resources FILES resources/AppIcon.png)
set_target_properties(CorbataPDF PROPERTIES
    OUTPUT_NAME "Corbata PDF"
    MACOSX_BUNDLE_BUNDLE_NAME "Corbata PDF"
    MACOSX_BUNDLE_GUI_IDENTIFIER "app.corbata.pdf"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/resources/Info.plist.in
)

# --- Tests for the PDF assembly logic
enable_testing()
add_executable(corbata_tests tests/test_writer.cpp)
target_link_libraries(corbata_tests PRIVATE corbata_core)
add_test(NAME writer COMMAND corbata_tests)

add_executable(corbata_fill_tests tests/test_fill.cpp)
target_link_libraries(corbata_fill_tests PRIVATE corbata_core Qt6::Pdf)
add_test(NAME fill COMMAND corbata_fill_tests)

# Drives the Fill & Sign window with simulated mouse and keyboard input.
add_executable(corbata_ui_tests tests/test_editor.cpp)
target_link_libraries(corbata_ui_tests PRIVATE corbata_ui Qt6::Test)
add_test(NAME editor COMMAND corbata_ui_tests)
set_tests_properties(editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
