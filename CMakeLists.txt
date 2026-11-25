cmake_minimum_required(VERSION 3.21)

project(CorbataPDF VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets Pdf)
find_package(qpdf CONFIG REQUIRED)

qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8 /W3)
endif()

# --- PDF assembly (Qt Core + qpdf only; shared by the app and the tests)
add_library(corbata_core STATIC src/PdfWriter.cpp src/PdfWriter.h src/PageRef.h)
target_include_directories(corbata_core PUBLIC src)
target_link_libraries(corbata_core PUBLIC Qt6::Core qpdf::libqpdf)
# vcpkg's static qpdf names its zlib/libjpeg dependencies without paths.
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
    target_link_directories(corbata_core PUBLIC
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/lib")
endif()

# --- The app
set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp src/MainWindow.h
    src/PageGrid.cpp src/PageGrid.h
    src/PageModel.cpp src/PageModel.h
    src/Library.cpp src/Library.h
    src/PreviewDialog.cpp src/PreviewDialog.h
)

if(WIN32)
    if(EXISTS ${CMAKE_SOURCE_DIR}/resources/app.ico)
        list(APPEND APP_SOURCES resources/app.rc)
    endif()
elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set(APP_ICNS ${CMAKE_SOURCE_DIR}/resources/AppIcon.icns)
    if(EXISTS ${APP_ICNS})
        set_source_files_properties(${APP_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        list(APPEND APP_SOURCES ${APP_ICNS})
    endif()
endif()

qt_add_executable(CorbataPDF WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_compile_definitions(CorbataPDF PRIVATE CORBATA_VERSION="${PROJECT_VERSION}")
target_link_libraries(CorbataPDF PRIVATE corbata_core Qt6::Widgets Qt6::Pdf)
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
