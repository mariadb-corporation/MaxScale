/*
 * This file is distributed as part of MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08-10-2014	Martin Brampton		Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hint.h>

/**
 * test1	Allocate table of users and mess around with it
 *
  */
int mxs_log_flush_sync(void);
static int
test1()
{
HINT    *hint;

        /* Hint tests */  
        ss_dfprintf(stderr,
                    "testhint : Add a parameter hint to a null list");
		char* name = strdup("name");
        hint = hint_create_parameter(NULL, name, "value");
		free(name);
        mxs_log_flush_sync();
        ss_info_dassert(NULL != hint, "New hint list should not be null");
        ss_info_dassert(0 == strcmp("value", hint->value), "Hint value should be correct");
        ss_info_dassert(0 != hint_exists(&hint, HINT_PARAMETER), "Hint of parameter type should exist");
        ss_dfprintf(stderr, "\t..done\nFree hints.");
        if (NULL != hint) hint_free(hint);
        mxs_log_flush_sync();
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();

	exit(result);
}

