<template>
    <div class="query-editor-page fill-height">
        <query-editor />
        <confirm-leave-dlg
            v-model="isConfDlgOpened"
            :onSave="onLeave"
            :shouldDelAll.sync="shouldDelAll"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions } from 'vuex'
import ConfirmLeaveDlg from '@queryEditorSrc/components/ConfirmLeaveDlg.vue'
import initEntities from '@queryEditorSrc/store/orm/initEntities'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'

export default {
    name: 'query-page',
    components: {
        ConfirmLeaveDlg,
    },
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
            if (Object.keys(this.allConns).length === 0) this.leavePage()
            else
                switch (to.path) {
                    case '/login':
                        this.leavePage()
                        break
                    case '/404':
                        this.cancelLeave()
                        this.validateConns({ persistentConns: this.allConns })
                        break
                    default:
                        this.shouldDelAll = true
                        this.isConfDlgOpened = true
                }
        }
    },
    async created() {
        initEntities()
        await this.validateConns({ persistentConns: this.allConns })
    },
    methods: {
        ...mapActions({
            validateConns: 'queryConns/validateConns',
            disconnectAll: 'queryConns/disconnectAll',
        }),
        async onLeave() {
            if (this.shouldDelAll) await this.disconnectAll()
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
