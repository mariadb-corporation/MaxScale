/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "dbfwfilter.cc"

int main(int argc, char **argv)
{
    int rval = 1;

    if (argc < 2)
    {
        printf("Usage: dbfw_rule_check FILE\n");
    }
    else
    {
        mxs_log_init("dbfwfilter_rule_parser", ".", MXS_LOG_TARGET_STDOUT);

        if (access(argv[1], R_OK) == 0)
        {
            MXS_NOTICE("Parsing rule file: %s", argv[1]);

            RuleList  rules;
            UserMap   users;

            if (process_rule_file(argv[1], &rules, &users))
            {
                MXS_NOTICE("Rule parsing was successful.");
                rval = 0;
            }
            else
            {
                MXS_ERROR("Failed to parse rules.");
            }
        }
        else
        {
            MXS_ERROR("Failed to read file '%s': %d, %s", argv[1], errno, strerror(errno));
        }

        mxs_log_finish();

    }

    return rval;
}
