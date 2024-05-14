/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ccdefs.hh>

#include <fstream>
#include <iostream>

#include <maxbase/string.hh>
#include <maxsimd/canonical.hh>

// Set this to 1 if you want the output to be colorized. This makes it easier to see
// differences in whitespace when viewed in a terminal.
#define INVERT_COLORS 0

std::string color(std::string_view msg)
{
#if INVERT_COLORS
    const char* COLOR_ON = "\033[7m";
    const char* COLOR_OFF = "\033[0m";
    return mxb::cat(COLOR_ON, msg, COLOR_OFF);
#else
    return std::string(msg);
#endif
}

std::string pretty_print(maxsimd::CanonicalArgs args)
{
    if (args.empty())
    {
        return "<none>";
    }

    return mxb::transform_join(args, [](const auto& arg){
        return MAKE_STR("(" << color(arg.value) << " at " << arg.pos << ")");
    });
}

size_t find_in_string(std::string_view str, const char* substring)
{
    std::string_view quotes = "\"'`";

    if (const char* start = mxb::strnchr_esc((char*)str.data(), substring, str.size(), quotes))
    {
        return std::distance(str.data(), start);
    }

    return std::string::npos;
}

std::string remove_comments(std::string str)
{
    size_t pos;

    while ((pos = find_in_string(str, "#")) != std::string::npos)
    {
        str.erase(pos);
    }

    while ((pos = find_in_string(str, "-- ")) != std::string::npos)
    {
        str.erase(pos);
    }

    while ((pos = find_in_string(str, "/*")) != std::string::npos)
    {
        if (pos + 2 < str.size() && (str[pos + 2] == 'M' || str[pos + 2] == '!'))
        {
            // Executable comment, don't remove it.
            break;
        }
        else if (auto pos_end = find_in_string(str.substr(pos + 2), "*/"); pos_end != std::string::npos)
        {
            // Fix it so that the position is relative to the original string and spans the whole comment.
            pos_end += pos + 4;

            if (pos_end == pos + 4)
            {
                // The "emptiest comment" requires special handling
                str.replace(pos, 4, " ");
            }
            else
            {
                // Normal comment, erase it. +2 is for the end of comment and the other +2 is due to the fact
                // that the position is relative to the end of the comment start.
                str.erase(pos, pos_end - pos);
            }
        }
        else
        {
            str.erase(pos);
        }
    }

    // TODO: This is not a valid comment and should not be replaced
    while ((pos = find_in_string(str, "/*/")) != std::string::npos)
    {
        str.erase(pos, 3);
    }

    // Handles the special case in one of the tests that ends with a trailing `/*`.
    while ((pos = find_in_string(str, "/*")) != std::string::npos)
    {
        if (auto pos_end = find_in_string(str.substr(pos), "/*"); pos_end == std::string::npos)
        {
            str.erase(pos);
        }
        else
        {
            break;
        }
    }
    return str;
}

/**
 * Reads the files given as the program arguments and generates canonical versions of each of the lines.
 *
 * @return 0 if all files produce identical results with both specialized and generic functions. 1 if at least
 *         one difference was found.
 */
int main(int argc, char** argv)
{
    int rc = EXIT_SUCCESS;

    if (argc < 2)
    {
        std::cout << "USAGE: " << argv[0] << " FILE\n";
        return EXIT_FAILURE;
    }

    int errors = 0;

    for (int i = 1; i < argc; i++)
    {
        std::ifstream infile(argv[i]);

        if (!infile)
        {
            std::cout << "Error opening file '" << argv[i] << "': " << mxb_strerror(errno) << "\n";
            rc = EXIT_FAILURE;
            break;
        }

        int lineno = 0;

        for (std::string line; std::getline(infile, line);)
        {
            ++lineno;
            std::string specialized = line;
            std::string generic = line;
            std::string old_generic = line;
            maxsimd::get_canonical(&specialized);
            maxsimd::generic::get_canonical(&generic);
            maxsimd::generic::get_canonical_old(&old_generic);

            if (specialized != generic || generic != old_generic)
            {
                std::cout << "Error at " << argv[i] << ":" << lineno << "\n"
                          << "in maxsimd::get_canonical \n"
                          << "Original:      " << color(line) << "\n"
                          << "Old generic:   " << color(old_generic) << "\n"
                          << "Generic:       " << color(generic) << "\n"
                          << "Specialized:   " << color(specialized) << "\n"
                          << "\n";
                ++errors;
            }

            // Test argument extraction
            maxsimd::CanonicalArgs args_specialized;
            maxsimd::CanonicalArgs args_generic;
            specialized = line;
            generic = line;
            maxsimd::get_canonical_args(&specialized, &args_specialized);
            maxsimd::generic::get_canonical_args(&generic, &args_generic);

            if (specialized != generic || args_specialized != args_generic)
            {
                std::cout << "Error at " << argv[i] << ":" << lineno << "\n"
                          << "in maxsimd::get_canonical_args \n"
                          << "Original:         " << color(line) << "\n"
                          << "Generic:          " << color(generic) << "\n"
                          << "Specialized:      " << color(specialized) << "\n"
                          << "Generic args:     " << pretty_print(args_generic) << "\n"
                          << "Specialized args: " << pretty_print(args_specialized) << "\n"
                          << "\n";
                ++errors;
            }

            // Test argument recombination
            auto sql_specialized = maxsimd::canonical_args_to_sql(specialized, args_specialized);
            auto sql_generic = maxsimd::canonical_args_to_sql(generic, args_generic);
            auto no_comments = remove_comments(line);

            if (sql_specialized != sql_generic || sql_specialized != no_comments)
            {
                std::cout << "Error at " << argv[i] << ":" << lineno << "\n"
                          << "in maxsimd::canonical_args_to_sql \n"
                          << "Original:         " << color(line) << "\n"
                          << "Without comments: " << color(no_comments) << "\n"
                          << "Generic:          " << color(sql_generic) << "\n"
                          << "Specialized:      " << color(sql_specialized) << "\n"
                          << "\n";
                ++errors;
            }
        }
    }

    if (errors)
    {
        std::cout << errors << " errors!" << std::endl;
        rc = EXIT_FAILURE;
    }

    return rc;
}
