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
 * 19-08-2014	Mark Riddoch		Initial implementation
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

#include <filter.h>


/**
 * test1 	Filter creation, finding and deletion
 *
 */
static int
test1()
{
FILTER_DEF	*f1, *f2;

	if ((f1 = filter_alloc("test1", "module")) == NULL)
	{
		fprintf(stderr, "filter_alloc: test 1 failed.\n");
		return 1;
	}
	if ((f2 = filter_find("test1")) == NULL)
	{
		fprintf(stderr, "filter_find: test 2 failed.\n");
		return 1;
	}
	filter_free(f1);
	if ((f2 = filter_find("test1")) != NULL)
	{
		fprintf(stderr, "filter_find: test 3 failed delete.\n");
		return 1;
	}

	return 0;
}


/**
 * Passive tests for filter_add_option and filter_add_parameter
 *
 * These tests add options and parameters to a filter, the only failure
 * is related hard crashes, such as SIGSEGV etc. as there are no good hooks
 * to check the creation of parameters and options currently.
 */
static int
test2()
{
FILTER_DEF	*f1;

	if ((f1 = filter_alloc("test1", "module")) == NULL)
	{
		fprintf(stderr, "filter_alloc: test 1 failed.\n");
		return 1;
	}
	filterAddOption(f1, "option1");
	filterAddOption(f1, "option2");
	filterAddOption(f1, "option3");
	filterAddParameter(f1, "name1", "value1");
	filterAddParameter(f1, "name2", "value2");
	filterAddParameter(f1, "name3", "value3");
	return 0;
}


/**
 * test3 	Filter creation, finding and deletion soak test
 *
 */
static int
test3()
{
FILTER_DEF	*f1;
char		name[40];
int		i, n_filters = 1000;

	for (i = 0; i < n_filters; i++)
	{
		sprintf(name, "filter%d", i);
		if ((f1 = filter_alloc(name, "module")) == NULL)
		{
			fprintf(stderr,
				"filter_alloc: test 3 failed with %s.\n", name);
			return 1;
		}
	}
	for (i = 0; i < n_filters; i++)
	{
		sprintf(name, "filter%d", i);
		if ((f1 = filter_find(name)) == NULL)
		{
			fprintf(stderr, "filter_find: test 3 failed.\n");
			return 1;
		}
	}
	for (i = 0; i < n_filters; i++)
	{
		sprintf(name, "filter%d", i);
		if ((f1 = filter_find(name)) == NULL)
		{
			fprintf(stderr, "filter_find: test 3 failed.\n");
			return 1;
		}
		filter_free(f1);
		if ((f1 = filter_find(name)) != NULL)
		{
			fprintf(stderr,
			"filter_find: test 3 failed - found deleted filter.\n");
			return 1;
		}
	}

	return 0;
}

int
main(int argc, char **argv)
{
int	result = 0;

	result += test1();
	result += test2();
	result += test3();

	exit(result);
}

