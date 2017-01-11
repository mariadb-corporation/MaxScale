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

#include "maskingfilterconfig.hh"

namespace
{

const char config_name_rules_file[]          = "rules_file";
const char config_name_warn_type_mismatch[]  = "warn_type_mismatch";

const char config_value_never[]  = "never";
const char config_value_always[] = "always";

}

//static
const char* MaskingFilterConfig::rules_file_name = config_name_rules_file;


//static
const char* MaskingFilterConfig::warn_type_mismatch_name = config_name_warn_type_mismatch;

//static
const MXS_ENUM_VALUE MaskingFilterConfig::warn_type_mismatch_values[] =
{
    { config_value_never,  MaskingFilterConfig::WARN_NEVER },
    { config_value_always, MaskingFilterConfig::WARN_ALWAYS },
    { NULL }
};

//static
const char* MaskingFilterConfig::warn_type_mismatch_default = config_value_never;


//static
MaskingFilterConfig::warn_type_mismatch_t
MaskingFilterConfig::get_warn_type_mismatch(const CONFIG_PARAMETER* pParams)
{
    int warn = config_get_enum(pParams, warn_type_mismatch_name, warn_type_mismatch_values);
    return static_cast<warn_type_mismatch_t>(warn);
}
