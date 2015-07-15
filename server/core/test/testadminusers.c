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
 * 20-08-2014	Mark Riddoch		Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gwdirs.h>
#include <adminusers.h>


/**
 * test1	default user
 *
 * Test that the username password admin/mariadb is accepted if no users
 * have been created and that no other users are accepted
 *
 * WARNING: The passwd file must be removed before this test is run
 */
static int
test1()
{
	if (admin_verify("admin", "mariadb") == 0)
	{
		fprintf(stderr, "admin_verify: test 1.1 (default user) failed.\n");
		return 1;
	}
	if (admin_verify("bad", "user"))
	{
		fprintf(stderr, "admin_verify: test 1.2 (wrong user) failed.\n");
		return 1;
	}

	return 0;
}

/**
 * test2	creating users
 *
 * Create a user
 * Try to create a duplicate user - expects a failure
 * Remove that user - expected to fail as one user must always remain
 */
static int
test2()
{
char	*err;

	if ((err = admin_add_user("user0", "passwd0")) != NULL)
	{
		fprintf(stderr, "admin_add_user: test 2.1 (add user) failed, %s.\n", err);

		return 1;
	}
	if (admin_add_user("user0", "passwd0") == NULL)
	{
		fprintf(stderr, "admin_add_user: test 2.2 (add user) failed, du;plicate.\n");

		return 1;
	}

	/* Deleting the last user is forbidden so we expect this to fail */
	if ((err = admin_remove_user("user0", "passwd0")) == NULL)
	{
		fprintf(stderr, "admin_remove_user: test 2.3 (add user) failed, %s.\n", err);

		return 1;
	}
	return 0;
}

/**
 * test3	search/verify users
 *
 * Create a user
 * Search for that user
 * Search for a non-existant user
 * Remove the user
 * Search for the user that was removed
 */
static int
test3()
{
char	*err;

	if ((err = admin_add_user("user1", "passwd1")) != NULL)
	{
		fprintf(stderr, "admin_add_user: test 3.1 (add user) failed, %s.\n", err);

		return 1;
	}

	if (admin_search_user("user1") == 0)
	{
		fprintf(stderr, "admin_search_user: test 3.2 (search user) failed.\n");

		return 1;
	}
	if (admin_search_user("user2") != 0)
	{
		fprintf(stderr, "admin_search_user: test 3.3 (search user) failed, unexpeted user found.\n");

		return 1;
	}

	if ((err = admin_remove_user("user1", "passwd1")) != NULL)
	{
		fprintf(stderr, "admin_remove_user: test 3.4 (add user) failed, %s.\n", err);

		return 1;
	}

	if (admin_search_user("user1"))
	{
		fprintf(stderr, "admin_search_user: test 3.5 (search user) failed - user was deleted.\n");

		return 1;
	}
	return 0;
}

/**
 * test4	verify users
 *
 * Create a numebr of users
 * search for each user in turn
 * verify each user in turn (password verification)
 * Verify each user in turn with incorrect password
 * Randomly verify each user
 * Remove each user
 */
static int
test4()
{
char	*err, user[40], passwd[40];
int	i, n_users = 50;

	for (i = 1; i < n_users; i++)
	{
		sprintf(user, "user%d", i);
		sprintf(passwd, "passwd%d", i);
		if ((err = admin_add_user(user, passwd)) != NULL)
		{
			fprintf(stderr, "admin_add_user: test 4.1 (add user) failed, %s.\n", err);

			return 1;
		}
	}

	for (i = 1; i < n_users; i++)
	{
		sprintf(user, "user%d", i);
		if (admin_search_user(user) == 0)
		{
			fprintf(stderr, "admin_search_user: test 4.2 (search user) failed.\n");

			return 1;
		}
	}
	for (i = 1; i < n_users; i++)
	{
		sprintf(user, "user%d", i);
		sprintf(passwd, "passwd%d", i);
		if (admin_verify(user, passwd) == 0)
		{
			fprintf(stderr, "admin_verify: test 4.3 (search user) failed.\n");

			return 1;
		}
	}

	for (i = 1; i < n_users; i++)
	{
		sprintf(user, "user%d", i);
		sprintf(passwd, "badpasswd%d", i);
		if (admin_verify(user, passwd) != 0)
		{
			fprintf(stderr, "admin_verify: test 4.4 (search user) failed.\n");

			return 1;
		}
	}
	srand(time(0));
	for (i = 1; i < 1000; i++)
	{
		int j;
		j = rand() % n_users;
		if (j == 0)
			j = 1;
		sprintf(user, "user%d", j);
		sprintf(passwd, "passwd%d", j);
		if (admin_verify(user, passwd) == 0)
		{
			fprintf(stderr, "admin_verify: test 4.5 (random) failed.\n");

			return 1;
		}
	}

	for (i = 1; i < n_users; i++)
	{
		sprintf(user, "user%d", i);
		sprintf(passwd, "passwd%d", i);
		if ((err = admin_remove_user(user, passwd)) != NULL)
		{
			fprintf(stderr, "admin_remove_user: test 4.6 (add user) failed, %s.\n", err);

			return 1;
		}
	}
	return 0;
}

/**
 * test5	remove first user
 *
 * Create a user so that user0 may be removed
 * Remove the first user created (user0)
 */
static int
test5()
{
char	*err;

	if ((err = admin_add_user("user", "passwd")) != NULL)
	{
		fprintf(stderr, "admin_add_user: test 5.1 (add user) failed, %s.\n", err);

		return 1;
	}
	if ((err = admin_remove_user("user0", "passwd0")) != NULL)
	{
		fprintf(stderr, "admin_remove_user: test 5.2 (add user) failed, %s.\n", err);

		return 1;
	}
	return 0;
}

int
main(int argc, char **argv)
{
int	result = 0;
char	*home, buf[1024];

	/* Unlink any existing password file before running this test */
	
	sprintf(buf, "%s/passwd", default_cachedir);
    if(!is_valid_posix_path(buf))
        exit(1);
	if (strcmp(buf, "/etc/passwd") != 0)
		unlink(buf);

	result += test1();
	result += test2();
	result += test3();
	result += test4();
	result += test5();

    /* Add the default user back so other tests can use it */
    admin_add_user("admin", "mariadb");

	exit(result);
}

