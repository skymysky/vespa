# Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
vespa_add_executable(searchlib_booleanmatchiteratorwrapper_test_app TEST
    SOURCES
    booleanmatchiteratorwrapper_test.cpp
    DEPENDS
    searchlib
    searchlib_test
)
vespa_add_test(NAME searchlib_booleanmatchiteratorwrapper_test_app COMMAND searchlib_booleanmatchiteratorwrapper_test_app)
