# Copyright 2008-present Contributors to the OpenImageIO project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


# Macro to add a build target for an IO plugin.
#
# Usage:
#
# add_oiio_plugin ( source1 [source2 ...]
#                   [ NAME targetname ... ]
#                   [ SRC source1 ... ]
#                   [ INCLUDE_DIRS include_dir1 ... ]
#                   [ LINK_LIBRARIES external_lib1 ... ]
#                   [ DEFINITIONS -DFOO=bar ... ])
#
# The plugin name can be specified with NAME, otherwise is inferred from the
# subdirectory name. The source files of the binary can be specifeied with
# SRC, otherwise are inferred to be all the .cpp files within the
# subdirectory. Optional compile DEFINITIONS, private INCLUDE_DIRS, and
# priviate LINK_LIBRARIES may also be specified. The source is automatically
# linked against OpenImageIO.
#
# The plugin may be disabled individually using any of the usual
# check_is_enabled() conventions (e.g. -DENABLE_<format>=OFF).
#
# What goes on under the covers is quite different depending on whether
# EMBEDPLUGINS is 0 or 1. If EMBEDPLUGINS is 0 (in which case this is
# expected to be called *after* the OpenImageIO target is declared), it will
# create a new target to build the full plugin. On the other hand, if
# EMBEDPLUGINS is 1 (in which case this should be called *before* the
# OpenImageIO target is declared), it will merely append the required
# definitions, includs, and libraries to lists format_plugin_blah that will
# be handed off too the setup of the later OpenImageIO target.
#
macro (add_oiio_plugin)
    cmake_parse_arguments (_plugin "" "NAME" "SRC;INCLUDE_DIRS;LINK_LIBRARIES;DEFINITIONS" ${ARGN})
       # Arguments: <prefix> <options> <one_value_keywords> <multi_value_keywords> args...
    get_filename_component (_plugin_name ${CMAKE_CURRENT_SOURCE_DIR} NAME_WE)
    if (NOT _plugin_NAME)
        # If NAME is not supplied, infer target name (and therefore the
        # executable name) from the directory name.
        get_filename_component (_plugin_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    endif ()
    # if (NOT _plugin_SRC)
    #     # If SRC is not supplied, assume local cpp files are its source.
    #     file (GLOB _plugin_SRC *.cpp)
    # endif ()
    check_is_enabled (${_plugin_name} _enable_plugin)
    if (_enable_plugin)
        if (EMBEDPLUGINS)
            # Add each source file to the libOpenImageIO_srcs, but it takes some
            # bending over backwards to change it in the parent scope.
            set (_plugin_all_source ${libOpenImageIO_srcs})
            foreach (_plugin_source_file ${_plugin_UNPARSED_ARGUMENTS} )
                list (APPEND _plugin_all_source "${CMAKE_CURRENT_SOURCE_DIR}/${_plugin_source_file}")
            endforeach ()
            set (libOpenImageIO_srcs "${_plugin_all_source}" PARENT_SCOPE)
            set (format_plugin_definitions ${format_plugin_definitions} ${_plugin_DEFINITIONS} PARENT_SCOPE)
            set (format_plugin_include_dirs ${format_plugin_include_dirs} ${_plugin_INCLUDE_DIRS} PARENT_SCOPE)
            set (format_plugin_libs ${format_plugin_libs} ${_plugin_LINK_LIBRARIES} PARENT_SCOPE)
        else ()
            # Get the name of the current directory and use it as the target name.
            get_filename_component (_plugin_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
            add_library (${_plugin_name} MODULE ${_plugin_UNPARSED_ARGUMENTS})
            target_compile_definitions (${_plugin_name} PRIVATE
                                        ${_plugin_DEFINITIONS}
                                        OpenImageIO_EXPORTS)
            target_include_directories (${_plugin_name} PRIVATE ${_plugin_INCLUDE_DIRS})
            target_link_libraries (${_plugin_name} PUBLIC OpenImageIO
                                                   PRIVATE ${_plugin_LINK_LIBRARIES})
            set_target_properties (${_plugin_name} PROPERTIES PREFIX "" FOLDER "Plugins")
            install_targets (${_plugin_name})
        endif ()
    else ()
        message (STATUS "${ColorRed}Disabling ${_plugin_name} ${ColorReset}")
        string (TOUPPER ${_plugin_name} _plugin_name_upper)
        set (format_plugin_definitions ${format_plugin_definitions} DISABLE_${_plugin_name_upper} PARENT_SCOPE)
    endif ()
endmacro ()



# Macro to add an executable build target. The executable name can be
# specified with NAME, otherwise is inferred from the subdirectory name. The
# source files of the binary can be specifeied with SRC, otherwise are
# inferred to be all the .cpp files within the subdirectory. Optional
# compile DEFINITIONS, private INCLUDE_DIRS, and priviate LINK_LIBRARIES may
# also be specified.
#
# The executable may be disabled individually using any of the usual
# check_is_enabled() conventions (e.g. -DENABLE_<executable>=OFF).
#
# Usage:
#
#   fancy_add_executable ([ NAME targetname ... ]
#                         [ SRC source1 ... ]
#                         [ INCLUDE_DIRS include_dir1 ... ]
#                         [ DEFINITIONS -DFOO=bar ... ])
#                         [ LINK_LIBRARIES external_lib1 ... ]
#
macro (fancy_add_executable)
    cmake_parse_arguments (_target "" "NAME" "SRC;INCLUDE_DIRS;SYSTEM_INCLUDE_DIRS;LINK_LIBRARIES;DEFINITIONS" ${ARGN})
       # Arguments: <prefix> <options> <one_value_keywords> <multi_value_keywords> args...
    if (NOT _target_NAME)
        # If NAME is not supplied, infer target name (and therefore the
        # executable name) from the directory name.
        get_filename_component (_target_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
    endif ()
    if (NOT _target_SRC)
        # If SRC is not supplied, assume local cpp files are its source.
        file (GLOB _target_SRC *.cpp)
    endif ()
    check_is_enabled (${_target_NAME} _target_NAME_enabled)
    if (_target_NAME_enabled)
        add_executable (${_target_NAME} ${_target_SRC})
        if (_target_INCLUDE_DIRS)
            target_include_directories (${_target_NAME} PRIVATE ${_target_INCLUDE_DIRS})
        endif ()
        if (_target_SYSTEM_INCLUDE_DIRS)
            target_include_directories (${_target_NAME} SYSTEM PRIVATE ${_target_SYSTEM_INCLUDE_DIRS})
        endif ()
        if (_target_DEFINITIONS)
            target_compile_definitions (${_target_name} PRIVATE ${_target_DEFINITIONS})
        endif ()
        if (_target_LINK_LIBRARIES)
            target_link_libraries (${_target_NAME} PRIVATE ${_target_LINK_LIBRARIES})
        endif ()
        set_target_properties (${_target_NAME} PROPERTIES FOLDER "Tools")
        install_targets (${_target_NAME})
    else ()
        message (STATUS "${ColorRed}Disabling ${_target_NAME} ${ColorReset}")
    endif ()
endmacro ()



# add_one_testsuite() - add environment variables to a test
#
# Usage:
#   add_one_testsuite (testname
#            testsrcdir - Current test directory in ${CMAKE_SOURCE_DIR}
#            [IMAGEDIR dir] - Optional path to image reference/compare directory
#            [ENV var=val...] - optional env variables to set
#            [COMMAND cmd...] - optional override of launch command
#    )
#
macro (add_one_testsuite testname testsrcdir)
    cmake_parse_arguments (_tst "" "IMAGEDIR;COST;PROCESSORS" "ENV;COMMAND" ${ARGN})
    set (testsuite "${CMAKE_SOURCE_DIR}/testsuite")
    set (testdir "${CMAKE_BINARY_DIR}/testsuite/${testname}")
    if (NOT _tst_COMMAND)
        set (_tst_COMMAND python "${testsuite}/runtest.py" ${testdir})
        if (MSVC_IDE)
            list (APPEND _tst_COMMAND --devenv-config $<CONFIGURATION>
                                      --solution-path "${CMAKE_BINARY_DIR}" )
        endif ()
    endif ()
    if (NOT _tst_COST)
        set (_tst_COST 1)
    endif ()
    if (NOT _tst_PROCESSORS)
        set (_tst_PROCESSORS 1)
    endif ()
    list (APPEND _tst_ENV
              OIIO_TESTSUITE_ROOT=${testsuite}
              OIIO_TESTSUITE_SRC=${testsrcdir}
              OIIO_TESTSUITE_CUR=${testdir}
              # OIIO_SOURCE_DIR=${CMAKE_SOURCE_DIR}
              # OIIO_BUILD_DIR=${CMAKE_BINARY_DIR}
         )
    if (_tst_IMAGEDIR)
        list (APPEND _tst_ENV OIIO_TESTSUITE_IMAGEDIR=${_tst_IMAGEDIR})
    endif()
    file (MAKE_DIRECTORY "${testdir}")
    add_test ( NAME ${testname} COMMAND ${_tst_COMMAND}
               #WORKING_DIRECTORY ${testdir}
               )
    set_tests_properties ( ${testname} PROPERTIES
                           ENVIRONMENT "${_tst_ENV}"
                           COST ${_tst_COST}
                           PROCESSORS ${_tst_PROCESSORS} )
    if (VERBOSE)
        message (STATUS "TEST ${testname}: ${_tst_COMMAND}  env: ${_tst_ENV}")
    endif ()
endmacro ()


# oiio_add_tests() - add a set of test cases.
#
# Usage:
#   oiio_add_tests ( test1 [ test2 ... ]
#                    [ IMAGEDIR name_of_reference_image_directory ]
#                    [ URL http://find.reference.cases.here.com ] )
#
# The optional argument IMAGEDIR is used to check whether external test images
# (not supplied with OIIO) are present, and to disable the test cases if
# they're not.  If IMAGEDIR is present, URL should also be included to tell
# the user where to find such tests.
#
macro (oiio_add_tests)
    cmake_parse_arguments (_ats "" "URL;IMAGEDIR;TESTNAME;FOUNDVAR;LABEL;COST;PROCESSORS" "PROPS" ${ARGN})
       # Arguments: <prefix> <options> <one_value_keywords> <multi_value_keywords> args...
    set (_ats_testdir "${OIIO_TESTSUITE_IMAGEDIR}/${_ats_IMAGEDIR}")
    # If there was a FOUNDVAR param specified and that variable name is
    # not defined, mark the test as broken.
    if (_ats_FOUNDVAR AND NOT ${_ats_FOUNDVAR})
        set (_ats_LABEL "broken")
    endif ()
    if (_ats_COST)
        list (APPEND _ats_PROPS COST ${_ats_COST})
    endif ()
    if (_ats_PROCESSORS)
        list (APPEND _ats_PROPS PROCESSORS ${_ats_PROCESSORS})
    endif ()
    if (_ats_IMAGEDIR AND NOT EXISTS ${_ats_testdir})
        # If the directory containig reference data (images) for the test
        # isn't found, point the user at the URL.
        message (STATUS "\n\nDid not find ${_ats_testdir}")
        message (STATUS "  -> Will not run tests ${_ats_UNPARSED_ARGUMENTS}")
        message (STATUS "  -> You can find it at ${_ats_URL}\n")
    else ()
        # Add the tests if all is well.
        set (_has_generator_expr TRUE)
        set (_testsuite "${CMAKE_SOURCE_DIR}/testsuite")
        foreach (_testname ${_ats_UNPARSED_ARGUMENTS})
            set (_testsrcdir "${_testsuite}/${_testname}")
            if (_ats_TESTNAME)
                set (_testname "${_ats_TESTNAME}")
            endif ()
            if (_ats_LABEL MATCHES "broken")
                set (_testname "${_testname}-broken")
            endif ()
            add_one_testsuite ("${_testname}" "${_testsrcdir}"
                               IMAGEDIR "${_ats_testdir}"
                               ${_ats_PROPS} )

            # For texture tests, add a second test using batch mode as well.
            if (_testname MATCHES "texture")
                add_one_testsuite ("${_testname}.batch" "${_testsrcdir}"
                                   IMAGEDIR "${_ats_testdir}"
                                   ENV TESTTEX_BATCH=1
                                   ${_ats_PROPS} )
            endif ()
        endforeach ()
        if (VERBOSE)
           message (STATUS "TESTS: ${_ats_UNPARSED_ARGUMENTS}")
        endif ()
    endif ()
endmacro ()

