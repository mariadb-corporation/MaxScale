#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file core/maxscale/filter.h - The private filter interface
 */

#include <maxscale/filter.h>

MXS_BEGIN_DECLS

void filter_add_option(MXS_FILTER_DEF *filter_def, const char *option);
void filter_add_parameter(MXS_FILTER_DEF *filter_def, const char *name, const char *value);
MXS_FILTER_DEF *filter_alloc(const char *name, const char *module_name);
MXS_DOWNSTREAM *filter_apply(MXS_FILTER_DEF *filter_def, SESSION *session, MXS_DOWNSTREAM *downstream);
void filter_free(MXS_FILTER_DEF *filter_def);
bool filter_load(MXS_FILTER_DEF *filter_def);
int filter_standard_parameter(const char *name);
MXS_UPSTREAM *filter_upstream(MXS_FILTER_DEF *filter_def, void *fsession, MXS_UPSTREAM *upstream);

MXS_END_DECLS
