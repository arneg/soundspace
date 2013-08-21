
find_package( Boost 1.40 COMPONENTS program_options REQUIRED )
include_directories( ${Boost_INCLUDE_DIR} )

find_package(JsonCpp REQUIRED)
include_directories(${JSONCPP_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES "${JSONCPP_LIB}")

add_library(json_builder json_builder.cpp)
add_library(interpol interpol.cpp)

install(TARGETS json_builder DESTINATION lib)
install(TARGETS interpol DESTINATION lib)
install(FILES interpol.h json_builder.h DESTINATION include)
