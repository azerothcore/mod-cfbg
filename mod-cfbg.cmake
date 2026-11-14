# unit_tests links only the static modules archive, so tests are added
# only when mod-cfbg is built into it.
if (BUILD_TESTING AND "${MODULE_MOD-CFBG}" STREQUAL "static")
    file(GLOB_RECURSE CFBG_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/mod-cfbg/tests/*.cpp")
    set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES
        ${CFBG_TEST_SOURCES})
    # unit_tests links game before modules; GNU ld needs game again after
    # modules for symbols only CFBG uses.
    target_link_libraries(modules INTERFACE game)
    message(STATUS "mod-cfbg: registered unit tests with unit_tests")
endif()
