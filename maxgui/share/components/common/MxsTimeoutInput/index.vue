<template>
    <mxs-label-field
        v-bind="{ ...$attrs }"
        :label="$mxs_t('timeout')"
        name="timeout"
        type="number"
        :required="true"
        v-on="$listeners"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'mxs-timeout-input',
    inheritAttrs: false,
}
</script>
