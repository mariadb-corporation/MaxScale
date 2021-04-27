#pragma once
#include <maxtest/test_dir.hh>

struct TestDefinition
{
    const char* name;
    const char* config_template;
    const char* labels;
};

extern const TestDefinition* test_definitions;
