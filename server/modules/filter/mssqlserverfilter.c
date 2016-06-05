#include <stdio.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <string.h>
#include <pcre2.h>
#include <atomic.h>
#include "maxconfig.h"

/**
 * @file mssqlserverfilter.c - a very filter  rewrite MS SQL Server 
 * Syntax to MariaDB syntax.
 * @verbatim
 *
 * A SQL server syntax translation filter.
 *
 * Date         Who             Description
 * 03/06/2016   Lisa Reilly Brinson    Addition of source and user parameters
 * @endverbatim
 */
