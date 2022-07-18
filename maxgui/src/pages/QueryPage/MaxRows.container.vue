<template>
    <max-rows
        v-model="value"
        :items="SQL_DEF_MAX_ROWS_OPTS"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
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
import MaxRows from './MaxRows.vue'
export default {
    name: 'max-rows-ctr',
    components: { MaxRows },
    inheritAttrs: false,
    computed: {
        ...mapState({
            SQL_DEF_MAX_ROWS_OPTS: state => state.app_config.SQL_DEF_MAX_ROWS_OPTS,
            query_max_rows: state => state.persisted.query_max_rows,
        }),
        value: {
            get() {
                return this.query_max_rows
            },
            set(value) {
                this.$emit('change', value)
            },
        },
    },
}
</script>
