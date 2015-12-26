###########################################################################
# Boost.Compute setup

find_path (Boost_Compute_INCLUDE_DIR boost/compute.hpp
           ${Boost_INCLUDE_DIRS}
           "${THIRD_PARTY_TOOLS}/include"
           "${PROJECT_SOURCE_DIR}/include"
           /usr/local/include
           /usr/local/include/compute
           /opt/local/include
           )

if (Boost_Compute_INCLUDE_DIR AND OpenCL_FOUND)
    set (Boost_Compute_FOUND TRUE)
    add_definitions ("-DUSE_BOOST_COMPUTE=1")
    if (NOT Boost_Compute_FIND_QUIETLY)
        message (STATUS "Found Boost.Compute")
        message (STATUS "Boost_Compute_INCLUDE_DIR = ${Boost_Compute_INCLUDE_DIR} ")
    endif ()
else ()
    set (Boost_Compute_FOUND FALSE)
    message (STATUS "Boost_Compute library not found")
endif ()

set (Boost_Compute_LIBRARIES ${Boost_Compute_LIBRARY})
set (Boost_Compute_INCLUDE_DIRS ${Boost_Compute_INCLUDE_DIR})

#include(${CMAKE_CURRENT_LIST_DIR}/FindPackageHandleStandardArgs.cmake)
find_package_handle_standard_args (Boost_Compute
    FOUND_VAR Boost_Compute_FOUND
    REQUIRED_VARS Boost_Compute_INCLUDE_DIR
#    VERSION_VAR Boost_Compute_VERSION
)

mark_as_advanced (Boost_Compute_INCLUDE_DIR)
