<template>
    <query-cnf-dlg
        v-bind="{ ...$attrs }"
        :cnf="{
            query_row_limit,
            query_confirm_flag,
            query_history_expired_time,
            query_show_sys_schemas_flag,
        }"
        v-on="$listeners"
        @confirm-save="save"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import QueryCnfDlg from './QueryCnfDlg.vue'
export default {
    name: 'query-cnf-dlg-ctr',
    components: {
        QueryCnfDlg,
    },
    inheritAttrs: false,
    computed: {
        ...mapState({
            query_row_limit: state => state.persisted.query_row_limit,
            query_confirm_flag: state => state.persisted.query_confirm_flag,
            query_history_expired_time: state => state.persisted.query_history_expired_time,
            query_show_sys_schemas_flag: state => state.persisted.query_show_sys_schemas_flag,
        }),
    },
    methods: {
        ...mapMutations({
            SET_QUERY_ROW_LIMIT: 'persisted/SET_QUERY_ROW_LIMIT',
            SET_QUERY_CONFIRM_FLAG: 'persisted/SET_QUERY_CONFIRM_FLAG',
            SET_QUERY_SHOW_SYS_SCHEMAS_FLAG: 'persisted/SET_QUERY_SHOW_SYS_SCHEMAS_FLAG',
            SET_QUERY_HISTORY_EXPIRED_TIME: 'persisted/SET_QUERY_HISTORY_EXPIRED_TIME',
        }),
        save(cnf) {
            this.SET_QUERY_ROW_LIMIT(cnf.query_row_limit)
            this.SET_QUERY_CONFIRM_FLAG(cnf.query_confirm_flag)
            this.SET_QUERY_HISTORY_EXPIRED_TIME(cnf.query_history_expired_time)
            this.SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(cnf.query_show_sys_schemas_flag)
        },
    },
}
</script>
