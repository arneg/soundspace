cmake_minimum_required (VERSION 2.6)
project (soundspace)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake" ${CMAKE_MODULE_PATH})

include_directories("${PROJECT_SOURCE_DIR}/libs")
set(EXTRA_LIBS ${EXTRA_LIBS} interpol json_builder)
add_subdirectory(libs)
include_directories(libs)

find_package( Boost 1.40 COMPONENTS program_options REQUIRED )
include_directories( ${Boost_INCLUDE_DIR} )

find_package(LibEvent REQUIRED)
include_directories(${LIBEVENT_INCLUDE_DIR})

find_package(JsonCpp REQUIRED)
include_directories(${JSONCPP_INCLUDE_DIR})

find_package(OpenAL REQUIRED)
include_directories(${OPENAL_INCLUDE_DIR})

set(EXTRA_LIBS ${EXTRA_LIBS} ${LIBEVENT_LIB} ${JSONCPP_LIB} ${OPENAL_LIBRARY}
    ${Boost_LIBRARIES})

add_executable(soundspace soundspace.cpp)
target_link_libraries(soundspace ${EXTRA_LIBS})

add_executable(test_soundspace soundspace.cpp)
target_link_libraries(test_soundspace ${EXTRA_LIBS})
set_target_properties(test_soundspace PROPERTIES COMPILE_DEFINITIONS "TESTING=1")

install_targets(/bin soundspace)
