/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <functional>
#include <memory>
#include <unistd.h>

class ExternalCmd
{
public:
    /**
     * Create a new external command. The name and parameters are copied so
     * the original memory can be freed.
     *
     * @param argstr Command to execute with the parameters
     * @param timeout Command timeout in seconds
     * @return Pointer to new external command struct or NULL if an error occurred
     */
    static std::unique_ptr<ExternalCmd> create(const std::string& argstr, int timeout);

    /**
     * Execute a command
     *
     * The output of the command must be freed by the caller by calling MXS_FREE.
     *
     * @return The return value of the executed command or -1 on error
     */
    int externcmd_execute();

    /**
     * If keyword is found in command script, replace keyword with output of generator function.
     *
     * @param keyword Keyword to replace
     * @param generator Function which generates the replacement string. Only ran if keyword was found.
     */
    void match_substitute(const std::string& keyword, const std::function<std::string(void)>& generator);

    /**
     * Reset substituted command to the unaltered command. Should be ran before a substitution pass begins.
     */
    void reset_substituted();

    const char* substituted() const;

private:
    static const int MAX_ARGS {256};

    std::string m_orig_command;        /**< Original command */
    std::string m_subst_command;       /**< Command with substitutions */
    int         m_timeout;             /**< Command timeout in seconds */

    ExternalCmd(const std::string& script, int timeout);

    int tokenize_args(char* dest[], int dest_size);

    /**
     * Substitute all occurrences of @c match with @c replace in the arguments.
     *
     * @param match Match string
     * @param replace Replacement string
     */
    void substitute_arg(const std::string& match, const std::string& replace);
};

