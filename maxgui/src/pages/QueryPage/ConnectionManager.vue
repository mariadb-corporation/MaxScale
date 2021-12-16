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
            :disabled="disabled"
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
        <connection-dialog
            v-model="isConnDialogOpened"
            :connOptions="connOptions"
            :handleSave="handleOpenConn"
            @on-cancel="assignActiveConn"
            @on-close="assignActiveConn"
        />
        <confirm-dialog
            v-model="isConfDlgOpened"
            :title="$t('disconnectConn')"
            type="disconnect"
            closeImmediate
            :item="connToBeDel"
            :onSave="confirmDelConn"
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
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import ConnectionDialog from './ConnectionDialog'
export default {
    name: 'connection-manager',
    components: {
        ConnectionDialog,
    },
    props: {
        disabled: { type: Boolean, required: true },
    },
    data() {
        return {
            isConnDialogOpened: false,
            chosenConn: {},
            targetConn: {}, // target connection to be deleted,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            cnct_resources: state => state.query.cnct_resources,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            worksheets_arr: state => state.query.worksheets_arr,
            active_wke_id: state => state.query.active_wke_id,
            conn_err_state: state => state.query.conn_err_state,
        }),
        ...mapGetters({
            getActiveWke: 'query/getActiveWke',
            getDbTreeData: 'query/getDbTreeData',
        }),
        /**
         * @returns connection ids that are bound to worksheets
         */
        usedConnections() {
            return this.worksheets_arr.reduce((acc, wke) => {
                const connId = this.$typy(wke, 'curr_cnct_resource.id').safeString
                if (connId) acc.push(connId)
                return acc
            }, [])
        },
        connOptions() {
            return this.cnct_resources.map(cnctRsrc =>
                this.curr_cnct_resource.id === cnctRsrc.id
                    ? { ...cnctRsrc, disabled: false }
                    : { ...cnctRsrc, disabled: this.usedConnections.includes(cnctRsrc.id) }
            )
        },
        connToBeDel() {
            return { id: this.targetConn.name }
        },
    },
    watch: {
        //Watcher to handle multi-worksheets
        curr_cnct_resource: {
            deep: true,
            async handler(v) {
                /**
                 * chosenConn is component'state, so when curr_cnct_resource query's module state
                 * is changed by changing worksheet, chosenConn needs to be updated by calling assignActiveConn
                 */
                if (!this.$help.lodash.isEqual(v, this.chosenConn)) {
                    this.assignActiveConn()
                    /**
                     * If the worksheet has an active connection but schema tree data which is stored
                     * in memory is an empty array, then call initialFetch to populate the data
                     */
                    if (this.getDbTreeData.length === 0) await this.handleDispatchIntialFetch(v)
                }
            },
        },
    },
    async created() {
        //Auto open dialog if there is no connections
        if (!this.connOptions.length) this.openConnDialog()
        else {
            this.assignActiveConn()
            await this.handleDispatchIntialFetch(this.curr_cnct_resource)
        }
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
            disconnect: 'query/disconnect',
            initialFetch: 'query/initialFetch',
        }),
        ...mapMutations({
            SET_CURR_CNCT_RESOURCE: 'query/SET_CURR_CNCT_RESOURCE',
            UPDATE_WKE: 'query/UPDATE_WKE',
        }),
        /**
         * Dispatching initalFetch when connection is valid, curr_cnct_resource
         * state is defined
         * @param {Object} curr_cnct_resource  curr_cnct_resource
         */
        async handleDispatchIntialFetch(curr_cnct_resource) {
            if (curr_cnct_resource.id) await this.initialFetch(curr_cnct_resource)
        },
        async onSelectConn(v) {
            if (this.$typy(v, 'id').isDefined) {
                this.SET_CURR_CNCT_RESOURCE({ payload: v, active_wke_id: this.active_wke_id })
                if (this.curr_cnct_resource.id) {
                    await this.initialFetch(v)
                }
            }
        },
        assignActiveConn() {
            if (this.curr_cnct_resource) this.chosenConn = this.curr_cnct_resource
            else this.chosenConn = {}
        },
        openConnDialog() {
            this.isConnDialogOpened = true
        },
        unlinkConn(item) {
            this.isConfDlgOpened = true
            this.targetConn = item
        },
        async handleOpenConn(opts) {
            /**
             *  When creating new connection, if current worksheet has been binded to
             *  a connection already, after successful connecting, dispatch initialFetch
             *  to reload schemas tree and other related components
             */

            const hasConnectionAlready = Boolean(this.curr_cnct_resource.id)
            await this.openConnect(opts)
            if (hasConnectionAlready && !this.conn_err_state)
                await this.initialFetch(this.curr_cnct_resource)
        },
        async confirmDelConn() {
            await this.disconnect({ showSnackbar: true, id: this.targetConn.id })
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
