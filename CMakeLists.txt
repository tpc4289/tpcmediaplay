cmake_minimum_required(VERSION 3.19)
project(tmediaplay LANGUAGES CXX)

# SET (FFMPEG_DIR "D:/developSoft/ffmpeg-8.1.2-full_build-shared")
SET (FFMPEG_DIR "E:/code/qt/lesson_code/ffmpeg")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets OpenGL OpenGLWidgets)

qt_standard_project_setup()

qt_add_executable(tmediaplay
    WIN32 MACOSX_BUNDLE
    main.cpp
    tmediaplay.cpp
    tmediaplay.h
    tmediaplay.ui
    tdemux.h tdemux.cpp
    tdemuxthread.h tdemuxthread.cpp
    tpacketqueue.h tpacketqueue.cpp
    tresample.h tresample.cpp
    tdecode.h tdecode.cpp
    tvideowidget.h tvideowidget.cpp
    tvideothread.h tvideothread.cpp
    taudiothread.h taudiothread.cpp
)
target_include_directories(tmediaplay PRIVATE ${FFMPEG_DIR}/include)

target_link_libraries(tmediaplay
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::OpenGLWidgets
        ${FFMPEG_DIR}/lib/avcodec.lib
        ${FFMPEG_DIR}/lib/avformat.lib
        ${FFMPEG_DIR}/lib/avutil.lib
        ${FFMPEG_DIR}/lib/swresample.lib
        ${FFMPEG_DIR}/lib/swscale.lib
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
