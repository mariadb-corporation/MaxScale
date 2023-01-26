<template>
    <div :style="{ maxWidth: '225px' }">
        <v-select
            v-model="chosenWkeConn"
            :items="wkeConnOpts"
            outlined
            dense
            class="vuetify-input--override v-select--mariadb conn-dropdown"
            :menu-props="{
                contentClass: 'v-select--menu-mariadb conn-menu',
                bottom: true,
                offsetY: true,
            }"
            :height="28"
            hide-details
            item-text="name"
            return-object
            :placeholder="$mxs_t('selectConnection')"
            :no-data-text="$mxs_t('noConnAvail')"
            :disabled="isConnBusy"
            @change="onSelectConn"
        >
            <template v-slot:selection="{ item }">
                <div class="d-flex align-center pl-1">
                    <v-icon class="mr-2" size="16" color="accent-dark">
                        $vuetify.icons.mxs_server
                    </v-icon>
                    <mxs-truncate-str
                        :tooltipItem="{ txt: item.name, nudgeLeft: 32 }"
                        :maxWidth="145"
                    />
                </div>
            </template>
            <template v-slot:item="{ item }">
                <v-list-item-title class="flex-row flex-grow-1">
                    <v-icon class="mr-2" size="16" color="accent-dark">
                        $vuetify.icons.mxs_server
                    </v-icon>
                    <mxs-truncate-str
                        :tooltipItem="{ txt: item.name, nudgeLeft: 32 }"
                        :maxWidth="135"
                    />
                    <v-spacer />
                    <mxs-tooltip-btn
                        btnClass="ml-2 disconnect-conn"
                        small
                        icon
                        fab
                        height="24"
                        width="24"
                        @click.stop="unlinkConn(item)"
                    >
                        <template v-slot:btn-content>
                            <v-icon size="18">
                                $vuetify.icons.mxs_unlink
                            </v-icon>
                        </template>
                        {{ $mxs_t('disconnect') }}
                    </mxs-tooltip-btn>
                </v-list-item-title>
            </template>
            <template v-slot:append-item>
                <v-divider />
                <v-list-item
                    ripple
                    link
                    class="text-decoration-underline mxs-color-helper text-primary"
                    @click="openConnDialog"
                >
                    {{ $mxs_t('newConnection') }}
                </v-list-item>
            </template>
        </v-select>
        <conn-dlg-ctr
            v-model="isConnDlgOpened"
            :wkeConnOpts="wkeConnOpts"
            :handleSave="handleOpenConn"
        />
        <mxs-conf-dlg
            v-model="isConfDlgOpened"
            :title="$mxs_t('disconnectConn')"
            saveText="disconnect"
            type="disconnect"
            :closeImmediate="true"
            :item="connToBeDel"
            :onSave="confirmDelConn"
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
import { mapState } from 'vuex'
import QueryConn from '@workspaceSrc/store/orm/models/QueryConn'
import Worksheet from '@workspaceSrc/store/orm/models/Worksheet'
import ConnDlgCtr from './ConnDlgCtr.vue'
import ReconnDlgCtr from './ReconnDlgCtr.vue'

export default {
    name: 'wke-conn-man',
    components: {
        ConnDlgCtr,
        ReconnDlgCtr,
    },
    data() {
        return {
            isConnDlgOpened: false,
            chosenWkeConn: {},
            targetConn: {}, // target connection to be deleted,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            pre_select_conn_rsrc: state => state.queryConnsMem.pre_select_conn_rsrc,
        }),
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        activeWkeConn() {
            return QueryConn.getters('getActiveWkeConn')
        },
        isConnBusy() {
            return QueryConn.getters('getIsConnBusyByActiveQueryTab')
        },
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
        connToBeDel() {
            return { id: this.targetConn.name }
        },
    },
    watch: {
        /**
         * Watcher to handle multi-worksheets
         */
        activeWkeId: {
            deep: true,
            immediate: true,
            async handler() {
                /**
                 * chosenWkeConn is component'state, so when activeWkeId is changed
                 * by changing worksheet, chosenWkeConn needs to be updated by calling assignActiveWkeConn
                 */
                this.assignActiveWkeConn()
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
            if (conn) {
                this.chosenWkeConn = conn
                await this.onSelectConn(conn)
            } else this.openConnDialog()
        },

        /**
         * Function is called after selecting a connection
         */
        async onSelectConn(chosenWkeConn) {
            await QueryConn.dispatch('onChangeWkeConn', chosenWkeConn)
            this.assignActiveWkeConn()
        },
        assignActiveWkeConn() {
            if (this.$typy(this.activeWkeConn).isEmptyObject) this.chosenWkeConn = {}
            else this.chosenWkeConn = this.activeWkeConn
        },
        openConnDialog() {
            this.isConnDlgOpened = true
        },
        unlinkConn(item) {
            this.isConfDlgOpened = true
            this.targetConn = item
        },
        async handleOpenConn(opts) {
            await QueryConn.dispatch('openWkeConn', opts)
            this.assignActiveWkeConn()
        },
        async confirmDelConn() {
            await QueryConn.dispatch('cascadeDisconnectWkeConn', {
                showSnackbar: true,
                id: this.targetConn.id,
            })
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

<style lang="scss">
.disconnect-conn {
    pointer-events: all;
}
.v-select--menu-mariadb.conn-menu .v-list .disconnect-conn {
    svg {
        color: $error !important;
    }
}
</style>
