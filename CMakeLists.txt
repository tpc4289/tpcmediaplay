cmake_minimum_required(VERSION 3.19)
project(tmediaplay LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(tmediaplay
    WIN32 MACOSX_BUNDLE
    main.cpp
    tmediaplay.cpp
    tmediaplay.h
    tmediaplay.ui
)

target_link_libraries(tmediaplay
    PRIVATE
        Qt::Core
        Qt::Widgets
)

install(TARGETS tmediaplay
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET tmediaplay
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
