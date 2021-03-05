# Copyright 2008-present Contributors to the OpenImageIO project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

set (CMAKE_AUTOMOC ON)
if (Qt5_POSITION_INDEPENDENT_CODE OR Qt6_POSITION_INDEPENDENT_CODE)
    set (CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

check_is_enabled (iv iv_enabled)
if (iv_enabled AND (Qt5_FOUND OR Qt6_FOUND) AND OPENGL_FOUND)
    fancy_add_executable (
        SYSTEM_INCLUDE_DIRS
            ${OPENGL_INCLUDE_DIR}
        LINK_LIBRARIES
            OpenImageIO
            $<$<BOOL:${Qt5_FOUND}>:Qt5::Core>
            $<$<BOOL:${Qt5_FOUND}>:Qt5::Gui>
            $<$<BOOL:${Qt5_FOUND}>:Qt5::Widgets>
            $<$<BOOL:${Qt5_FOUND}>:Qt5::OpenGL>
            $<$<BOOL:${Qt6_FOUND}>:Qt6::Core>
            $<$<BOOL:${Qt6_FOUND}>:Qt6::Gui>
            $<$<BOOL:${Qt6_FOUND}>:Qt6::Widgets>
            $<$<BOOL:${Qt6_FOUND}>:Qt6::OpenGL>
            ${OPENGL_LIBRARIES}
        )
    if (iv_enabled AND FORCE_OPENGL_1)
        target_compile_definitions(iv PRIVATE FORCE_OPENGL_1)
    endif()
else ()
    message (STATUS "\n\n   WARNING: Qt or OpenGL not found -- 'iv' will not be built!\n")
endif ()
