<template>
    <div>
        <conn-dlg-ctr
            v-model="isConnDlgOpened"
            :wkeConnOpts="wkeConnOpts"
            :handleSave="handleOpenConn"
        />
        <reconn-dlg-ctr :onReconnectCb="onReconnectCb" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * This component automatically opens connection dialog for creating new one if
 * there is no availabe connection. It also shows reconnection dialog
 */
import { mapState } from 'vuex'
import QueryConn from '@wsModels/QueryConn'
import ConnDlgCtr from '@wkeComps/QueryEditor/ConnDlgCtr.vue'
import ReconnDlgCtr from '@wsComps/ReconnDlgCtr.vue'

export default {
    name: 'conn-man',
    components: {
        ConnDlgCtr,
        ReconnDlgCtr,
    },
    data() {
        return {
            isConnDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            pre_select_conn_rsrc: state => state.queryConnsMem.pre_select_conn_rsrc,
        }),
        wkeConns() {
            return QueryConn.getters('getWkeConns')
        },
        // all connections having binding_type === QUERY_CONN_BINDING_TYPES.WORKSHEET
        wkeConnOpts() {
            return this.wkeConns.map(c => ({ ...c, disabled: Boolean(c.worksheet_id) }))
        },
        availableConnOpts() {
            return this.wkeConnOpts.filter(cnn => !cnn.disabled)
        },
    },
    watch: {
        pre_select_conn_rsrc: {
            async handler(v) {
                if (v) await this.handlePreSelectConnRsrc()
            },
        },
    },
    async created() {
        if (this.pre_select_conn_rsrc) await this.handlePreSelectConnRsrc()
        //Auto open dialog if there is no connections and pre_select_conn_rsrc
        else if (!this.wkeConnOpts.length) this.openConnDialog()
    },
    methods: {
        /**
         * Check if there is an available connection (connection that has not been bound to a worksheet),
         * bind it to the current worksheet. otherwise open dialog
         */
        async handlePreSelectConnRsrc() {
            const conn = this.availableConnOpts.find(
                conn => conn.name === this.pre_select_conn_rsrc.id
            )
            if (conn) await this.onSelectConn(conn)
            else this.openConnDialog()
        },

        /**
         * Function is called after selecting a connection
         */
        async onSelectConn(conn) {
            await QueryConn.dispatch('onChangeWkeConn', conn)
        },
        openConnDialog() {
            this.isConnDlgOpened = true
        },
        async handleOpenConn(opts) {
            await QueryConn.dispatch('openWkeConn', opts)
        },
        async onReconnectCb() {
            await QueryConn.dispatch('validateConns', {
                persistentConns: QueryConn.all(),
                silentValidation: true,
            })
        },
    },
}
</script>
