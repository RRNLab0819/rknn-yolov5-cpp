cmake_minimum_required(VERSION 3.10)
project(rknn_yolov5_demo)

if(ENABLE_ASAN)
    set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG}   -fno-omit-frame-pointer -fsanitize=address")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 REQUIRED COMPONENTS Widgets)

set(rknpu2_yolov5_file rknpu2/yolov5.cc)
if(TARGET_SOC STREQUAL "rv1106" OR TARGET_SOC STREQUAL "rv1103")
    add_definitions(-DRV1106_1103)
    set(rknpu2_yolov5_file rknpu2/yolov5_rv1106_1103.cc)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/allocator/dma)
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../3rdparty/ 3rdparty.out)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../utils/    utils.out)

set(CMAKE_INSTALL_RPATH "$ORIGIN/lib")

add_executable(${PROJECT_NAME}
    main.cc
    postprocess.cc
    postprocess.h
    yolov5.h
    ${rknpu2_yolov5_file}
    mainwindow.cc
    mainwindow.h
    camera.cc
    camera.h
    infer_thread.cc
    infer_thread.h
)

# RGA 库路径：优先从环境变量 RGA_ROOT 获取，否则使用 CMAKE_SOURCE_DIR 相对路径
set(RGA_ROOT "$ENV{RGA_ROOT}" CACHE PATH "Path to librga installation")
if(NOT RGA_ROOT)
    get_filename_component(RGA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../librga" ABSOLUTE)
endif()
message(STATUS "RGA_ROOT = ${RGA_ROOT}")

target_link_libraries(${PROJECT_NAME}
    imageutils
    fileutils
    imagedrawing
    ${LIBRKNNRT}
    pthread
    Qt5::Widgets
    ${RGA_ROOT}/libs/Linux/gcc-aarch64/librga.so
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} Threads::Threads)
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBRKNNRT_INCLUDES}
    ${RGA_ROOT}/include
)

install(TARGETS ${PROJECT_NAME} DESTINATION .)
file(GLOB RKNN_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../model/*.rknn")
install(FILES ${RKNN_FILES} DESTINATION model)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../model/coco_80_labels_list.txt DESTINATION model)
