set(JSONCPP_EXTRA_PREFIXES /usr/local /opt/local /usr/ "$ENV{HOME}")
foreach(prefix ${JSONCPP_EXTRA_PREFIXES})
    list(APPEND JSONCPP_INCLUDE_PATHS "${prefix}/include/jsoncpp")
    list(APPEND JSONCPP_INCLUDE_PATHS "${prefix}/include")
    list(APPEND JSONCPP_LIB_PATHS "${prefix}/lib/jsoncpp")
    list(APPEND JSONCPP_LIB_PATHS "${prefix}/lib")
endforeach()

find_path(JSONCPP_INCLUDE_DIR json/value.h PATHS ${JSONCPP_INCLUDE_PATHS})
find_library(JSONCPP_LIB NAMES jsoncpp PATHS ${JSONCPP_LIB_PATHS})

if (JSONCPP_LIB AND JSONCPP_INCLUDE_DIR)
    set(JSONCPP_FOUND TRUE)
  set(JSONCPP_LIB ${JSONCPP_LIB})
else ()
    set(JSONCPP_FOUND FALSE)
endif ()

if (JSONCPP_FOUND)
    if (NOT JSONCPP_FIND_QUIETLY)
      message(STATUS "Found jsoncpp: ${JSONCPP_LIB}")
  endif ()
else ()
    if (JSONCPP_FIND_REQUIRED)
        message(FATAL_ERROR "Could NOT find jsoncpp.")
    endif ()
    message(STATUS "jsoncpp NOT found.")
endif ()

mark_as_advanced(
    JSONCPP_LIB
    JSONCPP_INCLUDE_DIR
  )
