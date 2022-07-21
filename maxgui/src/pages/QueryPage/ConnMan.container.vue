<template>
    <div :style="{ maxWidth: '225px' }">
        <v-select
            v-model="chosenConn"
            :items="connOptions"
            outlined
            dense
            class="std mariadb-select-input conn-dropdown"
            :menu-props="{
                contentClass: 'mariadb-select-v-menu',
                bottom: true,
                offsetY: true,
            }"
            :height="28"
            hide-details
            item-text="name"
            return-object
            :placeholder="$t('selectConnection')"
            :no-data-text="$t('noConnAvail')"
            :disabled="getIsConnBusy"
            @change="onSelectConn"
        >
            <template v-slot:selection="{ item }">
                <div class="d-flex align-center pl-1">
                    <v-icon class="mr-2" size="16" color="accent-dark">
                        $vuetify.icons.server
                    </v-icon>
                    <truncate-string :text="item.name" :maxWidth="145" :nudgeLeft="32" />
                </div>
            </template>
            <template v-slot:item="{ item }">
                <div class="v-list-item__title d-flex align-center flex-row flex-grow-1">
                    <v-icon class="mr-2" size="16" :color="item.disabled ? '' : 'accent-dark'">
                        $vuetify.icons.server
                    </v-icon>
                    <truncate-string
                        class="text-truncate"
                        :text="item.name"
                        :maxWidth="135"
                        :nudgeLeft="32"
                    />
                    <v-spacer />
                    <v-tooltip
                        top
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                class="ml-2 disconnect-conn"
                                height="24"
                                width="24"
                                fab
                                icon
                                small
                                v-on="on"
                                @click.stop="unlinkConn(item)"
                            >
                                <v-icon size="18" color="error">
                                    $vuetify.icons.unlink
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('disconnect') }}</span>
                    </v-tooltip>
                </div>
            </template>
            <template v-slot:append-item>
                <v-divider />
                <v-list-item
                    ripple
                    link
                    class="text-decoration-underline color text-primary"
                    @click="openConnDialog"
                >
                    {{ $t('newConnection') }}
                </v-list-item>
            </template>
        </v-select>
        <conn-dlg-ctr
            v-model="isConnDlgOpened"
            :connOptions="connOptions"
            :handleSave="handleOpenConn"
        />
        <confirm-dialog
            v-model="isConfDlgOpened"
            :title="$t('disconnectConn')"
            type="disconnect"
            :closeImmediate="true"
            :item="connToBeDel"
            :onSave="confirmDelConn"
        />
        <reconn-dlg-ctr />
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
import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import ConnDlg from './ConnDlg.container.vue'
import ReconnDlg from './ReconnDlg.container.vue'

export default {
    name: 'conn-man-ctr',
    components: {
        'conn-dlg-ctr': ConnDlg,
        'reconn-dlg-ctr': ReconnDlg,
    },
    data() {
        return {
            isConnDlgOpened: false,
            chosenConn: {},
            targetConn: {}, // target connection to be deleted,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            sql_conns: state => state.queryConn.sql_conns,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            pre_select_conn_rsrc: state => state.queryConn.pre_select_conn_rsrc,
            active_wke_id: state => state.wke.active_wke_id,
            QUERY_CONN_BINDING_TYPES: state => state.app_config.QUERY_CONN_BINDING_TYPES,
            query_sessions: state => state.querySession.query_sessions,
        }),
        ...mapGetters({
            getActiveSessionId: 'querySession/getActiveSessionId',
            getActiveSession: 'querySession/getActiveSession',
            getSessionsOfActiveWke: 'querySession/getSessionsOfActiveWke',
            getIsConnBusy: 'queryConn/getIsConnBusy',
        }),
        defConnOfAllWkes() {
            return Object.values(this.sql_conns).filter(
                conn =>
                    conn.binding_type === this.QUERY_CONN_BINDING_TYPES.SESSION &&
                    !('clone_of_conn_id' in conn)
            )
        },
        /**
         * @returns connection ids that are bound to session tabs
         */
        usedConnections() {
            return this.query_sessions.reduce((acc, s) => {
                const connId = this.$typy(s, 'active_sql_conn.id').safeString
                if (connId) acc.push(connId)
                return acc
            }, [])
        },
        connOptions() {
            return this.defConnOfAllWkes.map(cnctRsrc =>
                this.active_sql_conn.id === cnctRsrc.id
                    ? { ...cnctRsrc, disabled: false }
                    : { ...cnctRsrc, disabled: this.usedConnections.includes(cnctRsrc.id) }
            )
        },
        availableConnOpts() {
            return this.connOptions.filter(cnn => !cnn.disabled)
        },
        connToBeDel() {
            return { id: this.targetConn.name }
        },
    },
    watch: {
        /**
         * Watcher to handle multi-worksheets
         */
        active_sql_conn: {
            deep: true,
            immediate: true,
            async handler(v) {
                /**
                 * chosenConn is component'state, so when active_sql_conn query's module state
                 * is changed by changing worksheet, chosenConn needs to be updated by calling assignActiveConn
                 */
                if (!this.$help.lodash.isEqual(v, this.chosenConn)) this.assignActiveConn(v)
            },
        },
        pre_select_conn_rsrc: {
            async handler(v) {
                if (v) await this.handlePreSelectConnRsrc()
            },
        },
    },
    async created() {
        if (this.pre_select_conn_rsrc) await this.handlePreSelectConnRsrc()
        //Auto open dialog if there is no connections and pre_select_conn_rsrc
        else if (!this.connOptions.length) this.openConnDialog()
    },
    methods: {
        ...mapActions({
            openConnect: 'queryConn/openConnect',
            syncSqlConnToSess: 'queryConn/syncSqlConnToSess',
            cloneAndSyncConnToSessions: 'queryConn/cloneAndSyncConnToSessions',
            disconnect: 'queryConn/disconnect',
            updateRoute: 'wke/updateRoute',
        }),
        ...mapMutations({ SET_ACTIVE_SQL_CONN: 'queryConn/SET_ACTIVE_SQL_CONN' }),
        /**
         * Check if there is an available connection (connection that has not been bound to a worksheet),
         * bind it to the current worksheet. otherwise open dialog
         */
        async handlePreSelectConnRsrc() {
            const conn = this.availableConnOpts.find(
                conn => conn.name === this.pre_select_conn_rsrc.id
            )
            if (conn) {
                this.chosenConn = conn
                await this.onSelectConn(conn)
            } else this.openConnDialog()
        },
        /**
         * Function is called after selecting a connection
         */
        async onSelectConn(chosenConn) {
            let activeSessConn = chosenConn
            const active_session_id = this.getActiveSessionId
            const fSess = this.getSessionsOfActiveWke[0]
            // sync the chosen connection to the first session tab
            this.syncSqlConnToSess({ sess: fSess, sql_conn: chosenConn })

            // Change the connection of other session tabs
            const otherSessTabs = this.getSessionsOfActiveWke.filter(s => s.id !== fSess.id)
            if (otherSessTabs.length) {
                const clonesOfChosenConn = Object.values(this.sql_conns).filter(
                    c =>
                        c.clone_of_conn_id === chosenConn.id &&
                        c.binding_type === this.QUERY_CONN_BINDING_TYPES.SESSION
                )
                const bondableSessTabs = otherSessTabs.slice(0, clonesOfChosenConn.length)
                const leftoverSessTabs = otherSessTabs.slice(clonesOfChosenConn.length)

                // Bind the existing cloned connections of the chosenConn to bondable session tabs
                for (const [i, s] of bondableSessTabs.entries()) {
                    this.syncSqlConnToSess({ sess: s, sql_conn: clonesOfChosenConn[i] })
                    if (active_session_id === s.id) activeSessConn = clonesOfChosenConn[i]
                }
                /**
                 * If there are more session tabs than the existing cloned connections, clone
                 * the chosenConn and bind it to those session tabs.
                 */
                if (leftoverSessTabs.length)
                    await this.cloneAndSyncConnToSessions({
                        sessions: leftoverSessTabs,
                        conn_to_be_cloned: chosenConn,
                        active_session_id,
                        getActiveSessConn: sessConn => (activeSessConn = sessConn),
                    })
            }

            // set active conn once sessions are bound to cloned connections to avoid concurrent query
            this.SET_ACTIVE_SQL_CONN({ payload: activeSessConn, id: active_session_id })
            // handle navigate to the corresponding nested route
            this.updateRoute(this.active_wke_id)
        },
        assignActiveConn(conn) {
            if (conn) this.chosenConn = conn
            else this.chosenConn = {}
        },
        openConnDialog() {
            this.isConnDlgOpened = true
        },
        unlinkConn(item) {
            this.isConfDlgOpened = true
            this.targetConn = item
        },
        async handleOpenConn(opts) {
            await this.openConnect(opts)
            // handle navigate to the corresponding nested route
            this.updateRoute(this.active_wke_id)
        },
        async confirmDelConn() {
            /**
             * Update route if the conn being deleted is bound to the current active worksheet.
             * Using the `name` property to check because the cloned conns will always have
             * the same name but with different id values
             * e.g server_0
             */
            const shouldUpdateRoute = this.active_sql_conn.name === this.targetConn.name
            await this.disconnect({ showSnackbar: true, id: this.targetConn.id })
            if (shouldUpdateRoute) this.updateRoute(this.active_wke_id)
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .mariadb-select-input.conn-dropdown {
    .v-input__control {
        min-height: 0px;
        fieldset {
            border: thin solid $accent-dark;
        }
    }
}
.disconnect-conn {
    pointer-events: all;
}
</style>
