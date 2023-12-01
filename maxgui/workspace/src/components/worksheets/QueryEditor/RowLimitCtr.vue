<template>
    <row-limit
        v-model="value"
        :items="DEF_ROW_LIMIT_OPTS"
        v-bind="{ ...$attrs }"
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Emits
 * $emit('change', v:number): new value
 */
import { mapState } from 'vuex'
import RowLimit from '@wkeComps/QueryEditor/RowLimit.vue'
export default {
    name: 'row-limit-ctr',
    components: { RowLimit },
    inheritAttrs: false,
    computed: {
        ...mapState({
            DEF_ROW_LIMIT_OPTS: state => state.mxsWorkspace.config.DEF_ROW_LIMIT_OPTS,
            query_row_limit: state => state.prefAndStorage.query_row_limit,
        }),
        value: {
            get() {
                return this.query_row_limit
            },
            set(value) {
                this.$emit('change', value)
            },
        },
    },
}
</script>
