<template>
    <div class="fill-height">
        <router-view />
        <confirm-dialog
            v-model="isConfDlgOpened"
            :title="$t('confirmations.leavePage')"
            type="thatsRight"
            minBodyWidth="624px"
            :onSave="onLeave"
            @on-close="cancelLeave"
            @on-cancel="cancelLeave"
        >
            <template v-slot:confirm-text>
                <p>{{ $t('info.disconnectAll') }}</p>
            </template>
            <template v-slot:body-append>
                <v-checkbox
                    v-model="confirmDelAll"
                    class="small"
                    :label="$t('disconnectAll')"
                    color="primary"
                    hide-details
                />
            </template>
        </confirm-dialog>
    </div>
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
import { mapActions, mapState, mapMutations, mapGetters } from 'vuex'
export default {
    name: 'query-page',
    data() {
        return {
            confirmDelAll: true,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            active_wke_id: state => state.wke.active_wke_id,
            sql_conns: state => state.queryConn.sql_conns,
        }),
        ...mapGetters({ getActiveWke: 'wke/getActiveWke' }),
    },
    watch: {
        $route: {
            immediate: true,
            async handler() {
                await this.chooseActiveWke()
            },
        },
        active_wke_id: {
            immediate: true,
            handler(v) {
                if (v) {
                    this.updateRoute(v)
                    this.handleSyncWke(this.getActiveWke)
                }
            },
        },
    },
    async created() {
        this.handleAutoClearQueryHistory()
        await this.validatingConn()
    },
    async beforeRouteLeave(to, from, next) {
        if (this.to) {
            next()
        } else {
            this.to = to
            /**
             * Allow to leave page immediately if next path is to login page (user logouts)
             * or there is no active connections or it's a redirection from '/query' to its nested route
             *
             */
            if (Object.keys(this.sql_conns).length === 0 || from.path === '/query') this.leavePage()
            else
                switch (to.path) {
                    case '/login':
                        this.leavePage()
                        break
                    case '/404':
                        this.SET_SNACK_BAR_MESSAGE({
                            text: [this.$t('info.notFoundConn')],
                            type: 'error',
                        })
                        this.cancelLeave()
                        this.clearConn()
                        await this.validatingConn()
                        break
                    default:
                        this.confirmDelAll = true
                        this.isConfDlgOpened = true
                }
        }
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE' }),
        ...mapActions({
            validatingConn: 'queryConn/validatingConn',
            disconnectAll: 'queryConn/disconnectAll',
            clearConn: 'queryConn/clearConn',
            updateRoute: 'wke/updateRoute',
            chooseActiveWke: 'wke/chooseActiveWke',
            handleSyncWke: 'wke/handleSyncWke',
            handleAutoClearQueryHistory: 'persisted/handleAutoClearQueryHistory',
        }),
        async onLeave() {
            if (this.confirmDelAll) await this.disconnectAll()
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
