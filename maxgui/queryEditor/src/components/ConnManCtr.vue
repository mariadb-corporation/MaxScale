<template>
    <div :style="{ maxWidth: '225px' }">
        <v-select
            v-model="chosenWkeConn"
            :items="connOptions"
            outlined
            dense
            class="vuetify-input--override mariadb-select-input conn-dropdown"
            :menu-props="{
                contentClass: 'mariadb-select-v-menu',
                bottom: true,
                offsetY: true,
            }"
            :height="28"
            hide-details
            item-text="name"
            return-object
            :placeholder="$mxs_t('selectConnection')"
            :no-data-text="$mxs_t('noConnAvail')"
            :disabled="getIsConnBusy"
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
                <div class="v-list-item__title d-flex align-center flex-row flex-grow-1">
                    <v-icon class="mr-2" size="16" :color="item.disabled ? '' : 'accent-dark'">
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
                            <v-icon size="18" color="error">$vuetify.icons.mxs_unlink</v-icon>
                        </template>
                        {{ $mxs_t('disconnect') }}
                    </mxs-tooltip-btn>
                </div>
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
            :connOptions="connOptions"
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
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapState } from 'vuex'
import ConnDlgCtr from './ConnDlgCtr.vue'
import ReconnDlgCtr from './ReconnDlgCtr.vue'

export default {
    name: 'conn-man-ctr',
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
            pre_select_conn_rsrc: state => state.queryConn.pre_select_conn_rsrc,
            active_wke_id: state => state.wke.active_wke_id,
            sql_conns: state => state.queryConn.sql_conns,
        }),
        ...mapGetters({
            getWkeConns: 'queryConn/getWkeConns',
            getCurrWkeConn: 'queryConn/getCurrWkeConn',
            getIsConnBusy: 'queryConn/getIsConnBusy',
        }),
        // all connections having binding_type === QUERY_CONN_BINDING_TYPES.WORKSHEET
        connOptions() {
            return this.getWkeConns.map(c => ({ ...c, disabled: Boolean(c.wke_id_fk) }))
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
        active_wke_id: {
            deep: true,
            immediate: true,
            async handler() {
                /**
                 * chosenWkeConn is component'state, so when active_wke_id is changed
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
        else if (!this.connOptions.length) this.openConnDialog()
    },
    methods: {
        ...mapActions({
            openConnect: 'queryConn/openConnect',
            disconnect: 'queryConn/disconnect',
            onChangeConn: 'queryConn/onChangeConn',
            validateConns: 'queryConn/validateConns',
        }),
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
            await this.onChangeConn(chosenWkeConn)
            this.assignActiveWkeConn()
        },
        assignActiveWkeConn() {
            if (this.$typy(this.getCurrWkeConn).isEmptyObject) this.chosenWkeConn = {}
            else this.chosenWkeConn = this.getCurrWkeConn
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
            this.assignActiveWkeConn()
        },
        async confirmDelConn() {
            await this.disconnect({ showSnackbar: true, id: this.targetConn.id })
        },
        async onReconnectCb() {
            await this.validateConns({ sqlConns: this.sql_conns, silentValidation: true })
        },
    },
}
</script>

<style lang="scss">
.conn-dropdown {
    .v-input__control {
        fieldset {
            border: thin solid $accent-dark !important;
        }
    }
}
.disconnect-conn {
    pointer-events: all;
}
</style>
