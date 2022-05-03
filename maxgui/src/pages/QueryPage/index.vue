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
 * Change Date: 2026-04-08
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
            active_wke_id: state => state.query.active_wke_id,
            sql_conns: state => state.queryConn.sql_conns,
        }),
        ...mapGetters({
            getActiveWke: 'query/getActiveWke',
        }),
    },
    watch: {
        $route: {
            immediate: true,
            handler() {
                this.chooseActiveWke()
            },
        },
        active_wke_id: {
            immediate: true,
            handler(v) {
                if (v) {
                    this.updateRoute(v)
                    this.SYNC_WKE_TO_QUERY_MODULE(this.getActiveWke)
                    this.SYNC_WKE_TO_QUERY_CONN_MODULE(this.getActiveWke)
                    this.SYNC_WKE_TO_SCHEMA_SIDEBAR_MODULE(this.getActiveWke)
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
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
            SYNC_WKE_TO_QUERY_MODULE: 'query/SYNC_WKE_TO_QUERY_MODULE',
            SYNC_WKE_TO_QUERY_CONN_MODULE: 'queryConn/SYNC_WKE_TO_QUERY_CONN_MODULE',
            SYNC_WKE_TO_SCHEMA_SIDEBAR_MODULE: 'schemaSidebar/SYNC_WKE_TO_SCHEMA_SIDEBAR_MODULE',
        }),
        ...mapActions({
            validatingConn: 'queryConn/validatingConn',
            disconnectAll: 'queryConn/disconnectAll',
            clearConn: 'queryConn/clearConn',
            updateRoute: 'query/updateRoute',
            chooseActiveWke: 'query/chooseActiveWke',
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
