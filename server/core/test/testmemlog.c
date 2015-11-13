/*
 * This file is distributed as part of MaxScale from MariaDB.  It is free
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
 * Copyright MariaDB Corporation 2014
 */

/**
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 30/09/2014	Mark Riddoch		Initial implementation
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
#include <unistd.h>
#include <string.h>
#include <memlog.h>

/**
 * Count the number of lines in a file
 *
 * @param file		The name of the file
 * @return	-1 if the file could not be opened or the numebr of lines
 */
int
linecount(char *file)
{
FILE		*fp;
int		i = 0;
char		buffer[180];

	if ((fp = fopen(file, "r")) == NULL)
		return -1;
	while (fgets(buffer, 180, fp) != NULL)
		i++;
	fclose(fp);
	return i;
}

/* Some strings to log */
char 	*strings[] = {
	"First log entry",
	"Second entry",
	"Third",
	"The fourth thing to log",
	"Add a final 5th item"
};

int
main()
{
MEMLOG		*log, *log2;
long int	i;
long		j;
long long	k;
int		failures = 0;

	unlink("memlog1");
	if ((log = memlog_create("memlog1", ML_INT, 100)) == NULL)
	{
		printf("Memlog Creation:		Failed\n");
		failures++;
	}
	else
	{
		printf("Memlog Creation:		Passed\n");
		if (access("memlog1",R_OK) == 0)
		{
			printf("File existance 1:		Failed\n");
			failures++;
		}
		else
			printf("File existance 1:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, (void *)i);
		if (access("memlog1",R_OK) == 0)
		{
			printf("File existance 2:		Failed\n");
			failures++;
		}
		else
			printf("File existance 2:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, (void *)i);
		if (access("memlog1",R_OK) != 0)
		{
			printf("File existance 3:		Failed\n");
			failures++;
		}
		else
			printf("File existance 3:		Passed\n");
		if (linecount("memlog1") != 100)
		{
			printf("Incorrect entry count:		Failed\n");
			failures++;
		}
		else
			printf("Incorrect entry count:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, (void *)i);
		if (linecount("memlog1") != 100)
		{
			printf("Premature Flushing:		Failed\n");
			failures++;
		}
		else
			printf("Premature Flushing:		Passed\n");
		memlog_destroy(log);
		if (linecount("memlog1") != 150)
		{
			printf("Flush on destroy:		Failed\n");
			failures++;
		}
		else
			printf("Flush on destroy:		Passed\n");
	}

	unlink("memlog2");
	if ((log = memlog_create("memlog2", ML_LONG, 100)) == NULL)
	{
		printf("Memlog Creation:		Failed\n");
		failures++;
	}
	else
	{
		printf("Memlog Creation:		Passed\n");
		if (access("memlog2",R_OK) == 0)
		{
			printf("File existance 1:		Failed\n");
			failures++;
		}
		else
			printf("File existance 1:		Passed\n");
		for (j = 0; j < 50; j++)
			memlog_log(log, (void *)j);
		if (access("memlog2",R_OK) == 0)
		{
			printf("File existance 2:		Failed\n");
			failures++;
		}
		else
			printf("File existance 2:		Passed\n");
		for (j = 0; j < 50; j++)
			memlog_log(log, (void *)j);
		if (access("memlog2",R_OK) != 0)
		{
			printf("File existance 3:		Failed\n");
			failures++;
		}
		else
			printf("File existance 3:		Passed\n");
		if (linecount("memlog2") != 100)
		{
			printf("Incorrect entry count:		Failed\n");
			failures++;
		}
		else
			printf("Incorrect entry count:		Passed\n");
		for (j = 0; j < 50; j++)
			memlog_log(log, (void *)j);
		if (linecount("memlog2") != 100)
		{
			printf("Premature Flushing:		Failed\n");
			failures++;
		}
		else
			printf("Premature Flushing:		Passed\n");
		memlog_destroy(log);
		if (linecount("memlog2") != 150)
		{
			printf("Flush on destroy:		Failed\n");
			failures++;
		}
		else
			printf("Flush on destroy:		Passed\n");
	}

	unlink("memlog3");
	if ((log = memlog_create("memlog3", ML_LONGLONG, 100)) == NULL)
	{
		printf("Memlog Creation:		Failed\n");
		failures++;
	}
	else
	{
		printf("Memlog Creation:		Passed\n");
		if (access("memlog3",R_OK) == 0)
		{
			printf("File existance 1:		Failed\n");
			failures++;
		}
		else
			printf("File existance 1:		Passed\n");
		for (k = 0; k < 50; k++)
			memlog_log(log, (void *)k);
		if (access("memlog3",R_OK) == 0)
		{
			printf("File existance 2:		Failed\n");
			failures++;
		}
		else
			printf("File existance 2:		Passed\n");
		for (k = 0; k < 50; k++)
			memlog_log(log, (void *)k);
		if (access("memlog3",R_OK) != 0)
		{
			printf("File existance 3:		Failed\n");
			failures++;
		}
		else
			printf("File existance 3:		Passed\n");
		if (linecount("memlog3") != 100)
		{
			printf("Incorrect entry count:		Failed\n");
			failures++;
		}
		else
			printf("Incorrect entry count:		Passed\n");
		for (k = 0; k < 50; k++)
			memlog_log(log, (void *)k);
		if (linecount("memlog3") != 100)
		{
			printf("Premature Flushing:		Failed\n");
			failures++;
		}
		else
			printf("Premature Flushing:		Passed\n");
		memlog_destroy(log);
		if (linecount("memlog3") != 150)
		{
			printf("Flush on destroy:		Failed\n");
			failures++;
		}
		else
			printf("Flush on destroy:		Passed\n");
	}

	unlink("memlog4");
	if ((log = memlog_create("memlog4", ML_STRING, 100)) == NULL)
	{
		printf("Memlog Creation:		Failed\n");
		failures++;
	}
	else
	{
		printf("Memlog Creation:		Passed\n");
		if (access("memlog4",R_OK) == 0)
		{
			printf("File existance 1:		Failed\n");
			failures++;
		}
		else
			printf("File existance 1:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, strings[i%5]);
		if (access("memlog4",R_OK) == 0)
		{
			printf("File existance 2:		Failed\n");
			failures++;
		}
		else
			printf("File existance 2:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, strings[i%5]);
		if (access("memlog4",R_OK) != 0)
		{
			printf("File existance 3:		Failed\n");
			failures++;
		}
		else
			printf("File existance 3:		Passed\n");
		if (linecount("memlog4") != 100)
		{
			printf("Incorrect entry count:		Failed\n");
			failures++;
		}
		else
			printf("Incorrect entry count:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, strings[i%5]);
		if (linecount("memlog4") != 100)
		{
			printf("Premature Flushing:		Failed\n");
			failures++;
		}
		else
			printf("Premature Flushing:		Passed\n");
		memlog_destroy(log);
		if (linecount("memlog4") != 150)
		{
			printf("Flush on destroy:		Failed\n");
			failures++;
		}
		else
			printf("Flush on destroy:		Passed\n");
	}

	unlink("memlog5");
	unlink("memlog6");
	if ((log = memlog_create("memlog5", ML_INT, 100)) == NULL)
	{
		printf("Memlog Creation:		Failed\n");
		failures++;
	}
	else
	{
		printf("Memlog Creation:		Passed\n");
		if ((log2 = memlog_create("memlog6", ML_INT, 100)) == NULL)
		{
			printf("Memlog Creation:		Failed\n");
			failures++;
		}
		else
		{
			printf("Memlog Creation:		Passed\n");
			for (i = 0; i < 40; i++)
				memlog_log(log, (void *)i);
			for (i = 0; i < 30; i++)
				memlog_log(log2, (void *)i);
			memlog_flush_all();
			if (linecount("memlog5") != 40 ||
					linecount("memlog6") != 30)
			{
				printf(
				"Memlog flush all:		Failed\n");
				failures++;
			}
			else
				printf(
				"Memlog flush all:		Passed\n");
		}
	}

	unlink("memlog7");
	if ((log = memlog_create("memlog7", ML_INT, 100)) == NULL)
	{
		printf("Memlog Creation:		Failed\n");
		failures++;
	}
	else
	{
		printf("Memlog Creation:		Passed\n");
		if (access("memlog7",R_OK) == 0)
		{
			printf("File existance 1:		Failed\n");
			failures++;
		}
		else
			printf("File existance 1:		Passed\n");
		for (i = 0; i < 5050; i++)
			memlog_log(log, (void *)i);
		if (access("memlog7",R_OK) != 0)
		{
			printf("File existance 3:		Failed\n");
			failures++;
		}
		else
			printf("File existance 3:		Passed\n");
		if (linecount("memlog7") != 5000)
		{
			printf("Incorrect entry count:		Failed\n");
			failures++;
		}
		else
			printf("Incorrect entry count:		Passed\n");
		for (i = 0; i < 50; i++)
			memlog_log(log, (void *)i);
		if (linecount("memlog7") != 5100)
		{
			printf("Residual flushing:		Failed\n");
			failures++;
		}
		else
			printf("Premature Flushing:		Passed\n");
		for (i = 0; i < 10120; i++)
			memlog_log(log, (void *)i);
		memlog_destroy(log);
		if (linecount("memlog7") != 15220)
		{
			printf("Flush on destroy:		Failed\n");
			failures++;
		}
		else
			printf("Flush on destroy:		Passed\n");
	}
	exit(failures);
}
