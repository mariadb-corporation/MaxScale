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
        <conn-man v-if="isActiveQueryEditorWorksheet" />
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
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'
import ConnMan from '@wkeComps/QueryEditor/ConnMan.vue'

export default {
    name: 'workspace-page',
    components: { ConfirmLeaveDlg, ConnMan },
    data() {
        return {
            isConfDlgOpened: false,
            shouldDelAll: true,
            to: '',
        }
    },
    computed: {
        allConns() {
            return QueryConn.all()
        },
        isActiveQueryEditorWorksheet() {
            return !this.$typy(Worksheet.getters('getActiveWke'), 'active_query_tab_id').isNull
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
    async beforeCreate() {
        await this.$store.dispatch('mxsWorkspace/initWorkspace')
    },
    async created() {
        await QueryConn.dispatch('validateConns', { persistentConns: this.allConns })
    },
    methods: {
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
    },
}
</script>
