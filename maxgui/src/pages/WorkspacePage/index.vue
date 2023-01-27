<template>
    <div class="mxs-workspace-page fill-height">
        <mxs-workspace />
        <confirm-leave-dlg
            v-model="isConfDlgOpened"
            :onSave="onLeave"
            :shouldDelAll.sync="shouldDelAll"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        />
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
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'
import ConnDlgCtr from '@wkeComps/QueryEditor/ConnDlgCtr.vue'
import ReconnDlgCtr from '@wsComps/ReconnDlgCtr.vue'
import { insertQueryTab } from '@wsSrc/store/orm/initEntities'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'workspace-page',
    components: { ConfirmLeaveDlg, ConnDlgCtr, ReconnDlgCtr },
    data() {
        return {
            isConfDlgOpened: false,
            shouldDelAll: true,
            to: '',
        }
    },
    computed: {
        ...mapState({
            is_conn_dlg_opened: state => state.mxsWorkspace.is_conn_dlg_opened,
            pre_select_conn_rsrc: state => state.queryConnsMem.pre_select_conn_rsrc,
        }),
        allConns() {
            return QueryConn.all()
        },
        // all connections having binding_type === QUERY_CONN_BINDING_TYPES.WORKSHEET
        wkeConnOpts() {
            return QueryConn.getters('getWkeConns').map(c => ({
                ...c,
                disabled: Boolean(c.worksheet_id),
            }))
        },
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        isConnDlgOpened: {
            get() {
                return this.is_conn_dlg_opened
            },
            set(v) {
                this.SET_IS_CONN_DLG_OPENED(v)
            },
        },
        wkeConns() {
            return QueryConn.getters('getWkeConns')
        },
        availableConnOpts() {
            return this.wkeConnOpts.filter(cnn => !cnn.disabled)
        },
    },
    beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            /**
             * Allow to leave page immediately if next path is to login page (user logouts)
             * or if there is no active connections
             */
            if (this.allConns.length === 0) this.leavePage()
            else
                switch (to.path) {
                    case '/login':
                        this.leavePage()
                        break
                    case '/404':
                        this.cancelLeave()
                        QueryConn.dispatch('validateConns', { persistentConns: this.allConns })
                        break
                    default:
                        this.shouldDelAll = true
                        this.isConfDlgOpened = true
                }
        }
    },
    watch: {
        pre_select_conn_rsrc: {
            immediate: true,
            async handler(v) {
                if (v) await this.handlePreSelectConnRsrc()
            },
        },
    },
    async beforeCreate() {
        await this.$store.dispatch('mxsWorkspace/initWorkspace')
    },
    async created() {
        await QueryConn.dispatch('validateConns', { persistentConns: this.allConns })
    },
    methods: {
        ...mapMutations({ SET_IS_CONN_DLG_OPENED: 'mxsWorkspace/SET_IS_CONN_DLG_OPENED' }),
        async onLeave() {
            if (this.shouldDelAll) await QueryConn.dispatch('disconnectAll')
            this.leavePage()
        },
        leavePage() {
            this.$router.push(this.to)
        },
        cancelLeave() {
            this.to = null
        },
        /**
         * Init QueryEditor entities if they aren't existed for
         * the active worksheet.
         */
        initQueryEditorEntities() {
            if (!QueryEditorTmp.find(this.activeWkeId))
                QueryEditorTmp.insert({ data: { id: this.activeWkeId } })
            if (!SchemaSidebar.find(this.activeWkeId))
                SchemaSidebar.insert({ data: { id: this.activeWkeId } })
            if (
                !QueryTab.query()
                    .where('worksheet_id', this.activeWkeId)
                    .first()
            )
                insertQueryTab(this.activeWkeId)
        },
        async handleOpenConn(opts) {
            // First initialize QueryEditor orm entities
            this.initQueryEditorEntities()
            /**
             * TODO: Refactor openWkeConn so initQueryEditorEntities won't have to be call first,
             * initQueryEditorEntities should be called only after connection is created succesfully
             */
            await QueryConn.dispatch('openWkeConn', opts)
        },

        async onReconnectCb() {
            await QueryConn.dispatch('validateConns', {
                persistentConns: QueryConn.all(),
                silentValidation: true,
            })
        },

        /**
         * Check if there is an available connection (connection that has not been bound to a worksheet),
         * bind it to the current worksheet. otherwise open dialog
         */
        async handlePreSelectConnRsrc() {
            const conn = this.availableConnOpts.find(
                conn => conn.name === this.pre_select_conn_rsrc.id
            )
            if (conn) await await QueryConn.dispatch('onChangeWkeConn', conn)
            else this.SET_IS_CONN_DLG_OPENED(true)
        },
    },
}
</script>
