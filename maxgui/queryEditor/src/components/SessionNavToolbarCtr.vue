<template>
    <div
        class="session-nav-toolbar-ctr d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border"
    >
        <div ref="buttonWrapper" class="d-flex align-center px-2">
            <v-btn
                :disabled="$typy(active_sql_conn).isEmptyObject"
                small
                class="float-left add-sess-btn"
                icon
                @click="handleAddNewSession({ wke_id: active_wke_id })"
            >
                <v-icon size="18" color="deep-ocean">mdi-plus</v-icon>
            </v-btn>
        </div>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState } from 'vuex'

export default {
    name: 'session-nav-toolbar-ctr',
    computed: {
        ...mapState({
            active_sql_conn: state => state.queryConn.active_sql_conn,
            active_wke_id: state => state.wke.active_wke_id,
        }),
    },
    mounted() {
        this.$nextTick(() =>
            this.$emit('get-total-btn-width', this.$refs.buttonWrapper.clientWidth)
        )
    },
    methods: {
        ...mapActions({ handleAddNewSession: 'querySession/handleAddNewSession' }),
    },
}
</script>
