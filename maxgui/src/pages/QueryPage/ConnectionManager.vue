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
            <template v-slot:item="{ item, on, attrs }">
                <div
                    class="v-list-item__title d-flex align-center flex-row flex-grow-1"
                    v-bind="attrs"
                    v-on="on"
                >
                    <div
                        v-if="item.name === newConnOption.name"
                        class="text-decoration-underline color text-primary"
                    >
                        {{ item.name }}
                    </div>
                    <template v-else>
                        <v-icon class="mr-2" size="16" :color="item.disabled ? '' : 'accent-dark'">
                            $vuetify.icons.server
                        </v-icon>
                        <truncate-string :text="item.name" :maxWidth="135" :nudgeLeft="32" />
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
                                    @click.stop="() => unlinkConn(item)"
                                >
                                    <v-icon size="18" color="error">
                                        $vuetify.icons.unlink
                                    </v-icon>
                                </v-btn>
                            </template>
                            <span>{{ $t('disconnect') }}</span>
                        </v-tooltip>
                    </template>
                </div>
            </template>
        </v-select>
        <connection-dialog
            v-model="isConnDialogOpened"
            :connOptions="connOptions"
            :handleSave="handleOpenConn"
            :onCancel="assignActiveConn"
            :onClose="assignActiveConn"
        />
        <confirm-dialog
            ref="confirmDialog"
            :title="$t('disconnectConn')"
            type="disconnect"
            closeImmediate
            :item="{ id: targetConn.name }"
            :onSave="() => disconnect({ showSnackbar: true, id: targetConn.id })"
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
 * Change Date: 2025-08-17
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
            newConnOption: {
                name: this.$t('newConnection'),
            },
            targetConn: {}, // target connection to be deleted
        }
    },
    computed: {
        ...mapState({
            is_validating_conn: state => state.query.is_validating_conn,
            cnct_resources: state => state.query.cnct_resources,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            worksheets_arr: state => state.query.worksheets_arr,
            active_wke_id: state => state.query.active_wke_id,
            conn_err_state: state => state.query.conn_err_state,
        }),
        ...mapGetters({
            getActiveWke: 'query/getActiveWke',
        }),
        usedConnections() {
            return this.worksheets_arr.map(
                wke => this.$typy(wke, 'curr_cnct_resource.id').safeString
            )
        },
        connOptions() {
            let options = [this.newConnOption]
            const allCnctResources = JSON.parse(JSON.stringify(this.cnct_resources))
            if (allCnctResources.length) {
                allCnctResources.forEach(cnctRsrc => {
                    if (this.curr_cnct_resource.id === cnctRsrc.id) cnctRsrc.disabled = false
                    else cnctRsrc.disabled = this.usedConnections.includes(cnctRsrc.id)
                })
                options.unshift(...allCnctResources)
            }
            return options
        },
        isCreatingNewConn() {
            return this.chosenConn.name === this.newConnOption.name
        },
    },
    watch: {
        is_validating_conn(v) {
            //After finish validating connections, auto open dialog if there is no connections
            if (!v && this.connOptions.length === 1) this.openConnDialog()
            else this.assignActiveConn()
        },
        curr_cnct_resource(v) {
            if (!this.$help.lodash.isEqual(v, this.chosenConn)) this.chosenConn = v
        },
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
        async onSelectConn(v) {
            if (this.isCreatingNewConn) this.openConnDialog()
            else if (this.$typy(v, 'id').isDefined) {
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
            this.$refs.confirmDialog.open()
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
