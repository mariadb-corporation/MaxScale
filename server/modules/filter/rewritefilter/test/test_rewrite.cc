/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../rf_reader.hh"
#include "../sql_rewriter.hh"
#include <iostream>

struct ReplacementError
{
    ReplacementError(const std::string& input,
                     const std::string& output,
                     const std::string& expected_output)
        : input(input)
        , output(output)
        , expected_output(expected_output)
    {
    }
    const std::string input;
    const std::string output;
    const std::string expected_output;
};

int main(int argc, char* argv[])
try
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " unit-test-file" << std::endl;
        return EXIT_FAILURE;
    }

    auto unit_test_file = argv[1];

    std::vector<ReplacementError> errors;
    TemplateDef default_template;
    auto templates = read_templates_from_rf(unit_test_file, default_template);
    auto rewriters = create_rewriters(templates);
    std::string replacement;
    bool continued = false;

    for (const auto& rewriter : rewriters)
    {
        const auto def = rewriter->template_def();
        if (def.continue_if_matched && def.unit_test_input.size() > 1)
        {
            MXB_THROW(RewriteError, "Cannot define multiple unit tests for an"
                                    " entry with continue_if_matched==true");
        }

        for (size_t i = 0; i < def.unit_test_input.size(); ++i)
        {
            std::string input = continued ? replacement : def.unit_test_input[i];
            continued = false;

            bool matched = rewriter->replace(input, &replacement);
            if (!matched && def.unit_test_output[i].length() == 0)
            {
                // pass. Ok, should not match when the unit_test_output is empty
            }
            else if (!matched || replacement != def.unit_test_output[i])
            {
                auto rep = matched ? replacement : "<input did not match>";
                errors.emplace_back(def.unit_test_input[i],
                                    rep,
                                    def.unit_test_output[i]);
            }
            else if (matched && def.continue_if_matched)
            {
                continued = true;
            }
        }
    }

    bool have_errors = false;
    for (const auto& error : errors)
    {
        have_errors = true;
        std::cerr << "Input:   " << error.input
                  << "\nOutput:  " << error.output
                  << "\nExpected " << error.expected_output
                  << std::endl;
    }
    return have_errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
catch (const std::exception& ex)
{
    std::cerr << ex.what() << std::endl;
    return EXIT_FAILURE;
}
