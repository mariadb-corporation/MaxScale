<template>
    <query-cnf-dlg
        v-bind="{ ...$attrs }"
        :cnf="{
            query_row_limit,
            query_confirm_flag,
            query_history_expired_time,
            query_show_sys_schemas_flag,
            tab_moves_focus,
            max_statements,
        }"
        v-on="$listeners"
        @confirm-save="save"
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
 * Change Date: 2027-08-18
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
            query_row_limit: state => state.prefAndStorage.query_row_limit,
            query_confirm_flag: state => state.prefAndStorage.query_confirm_flag,
            query_history_expired_time: state => state.prefAndStorage.query_history_expired_time,
            query_show_sys_schemas_flag: state => state.prefAndStorage.query_show_sys_schemas_flag,
            tab_moves_focus: state => state.prefAndStorage.tab_moves_focus,
            max_statements: state => state.prefAndStorage.max_statements,
        }),
    },
    methods: {
        ...mapMutations({
            SET_QUERY_ROW_LIMIT: 'prefAndStorage/SET_QUERY_ROW_LIMIT',
            SET_QUERY_CONFIRM_FLAG: 'prefAndStorage/SET_QUERY_CONFIRM_FLAG',
            SET_QUERY_SHOW_SYS_SCHEMAS_FLAG: 'prefAndStorage/SET_QUERY_SHOW_SYS_SCHEMAS_FLAG',
            SET_QUERY_HISTORY_EXPIRED_TIME: 'prefAndStorage/SET_QUERY_HISTORY_EXPIRED_TIME',
            SET_TAB_MOVES_FOCUS: 'prefAndStorage/SET_TAB_MOVES_FOCUS',
            SET_MAX_STATEMENTS: 'prefAndStorage/SET_MAX_STATEMENTS',
        }),
        save(cnf) {
            this.SET_QUERY_ROW_LIMIT(cnf.query_row_limit)
            this.SET_QUERY_CONFIRM_FLAG(cnf.query_confirm_flag)
            this.SET_QUERY_HISTORY_EXPIRED_TIME(cnf.query_history_expired_time)
            this.SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(cnf.query_show_sys_schemas_flag)
            this.SET_TAB_MOVES_FOCUS(cnf.tab_moves_focus)
            this.SET_MAX_STATEMENTS(cnf.max_statements)
        },
    },
}
</script>
