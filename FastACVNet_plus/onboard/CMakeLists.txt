cmake_minimum_required (VERSION 3.8)

project(fastacvnet_plus)
set(CMAKE_CXX_FLAGS "-std=c++11 -Wall -O0 -g -rdynamic -g2 -ggdb")


SET(LIBRARY_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/FastACVNet_plus/lib)

find_package(OpenCV REQUIRED)
find_package(CUDA REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(rviz_visual_tools REQUIRED)


include_directories(${CMAKE_SOURCE_DIR}/FastACVNet_plus/include)
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(/usr/local/cuda/include)

file(GLOB lib_header "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
file(GLOB common_header "${CMAKE_SOURCE_DIR}/common/*.h")

file(GLOB lib_src "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB common_src "${CMAKE_SOURCE_DIR}/common/*.cpp")


include_directories(${TensorRT_INCLUDE})
link_directories(${TensorRT_LIB})

cuda_add_library(${PROJECT_NAME} SHARED ${lib_src} ${common_src} FastACVNet_plus_preprocess.cu)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
    target_link_libraries(${PROJECT_NAME} "/usr/local/cuda/targets/aarch64-linux/lib/libcudart.so")
else()
    target_link_libraries(${PROJECT_NAME} "/usr/local/cuda/lib64/libcudart.so")
endif()
target_link_libraries(${PROJECT_NAME} "${TensorRT_LIB}/libnvinfer.so")
target_link_libraries(${PROJECT_NAME} "${TensorRT_LIB}/libnvonnxparser.so")
target_link_libraries(${PROJECT_NAME} "${TensorRT_LIB}/libnvinfer_plugin.so")
target_link_libraries(${PROJECT_NAME} "${TensorRT_LIB}/libcudnn.so")
target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})
