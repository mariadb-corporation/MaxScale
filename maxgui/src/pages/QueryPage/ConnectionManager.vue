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
                        <v-icon class="mr-2" size="16" color="accent-dark">
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
                                    class="ml-2"
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
            :handleSave="handleOpenConn"
            :onCancel="assignActiveConn"
            :onClose="assignActiveConn"
        />
        <confirm-dialog
            ref="confirmDialog"
            :title="$t('disconnectConn')"
            type="disconnect"
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
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapMutations, mapState } from 'vuex'
import ConnectionDialog from './ConnectionDialog'
export default {
    name: 'connection-manager',
    components: {
        ConnectionDialog,
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
            is_checking_active_conn: state => state.query.is_checking_active_conn,
            cnct_resources: state => state.query.cnct_resources,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_conn_state: state => state.query.active_conn_state,
            expanded_nodes: state => state.query.expanded_nodes,
        }),
        connOptions() {
            let options = [this.newConnOption]
            if (this.cnct_resources.length) options.unshift(...this.cnct_resources)
            return options
        },
        isCreatingNewConn() {
            return this.chosenConn.name === this.newConnOption.name
        },
    },
    watch: {
        is_checking_active_conn(v) {
            //After finish checking is_checking_active_conn, auto open dialog if there is no active connection
            if (!v && this.connOptions.length === 1) this.openConnDialog()
            else this.assignActiveConn()
        },
        chosenConn: {
            deep: true,
            async handler(v) {
                if (this.isCreatingNewConn) this.openConnDialog()
                else if (this.$typy(v, 'id').isDefined) {
                    this.SET_CURR_CNCT_RESOURCE(v)
                    await this.checkActiveConn()
                    if (this.active_conn_state) {
                        await this.initialFetch()
                    }
                }
            },
        },
        curr_cnct_resource(v) {
            if (!this.$help.lodash.isEqual(v, this.chosenConn)) this.chosenConn = v
        },
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
            disconnect: 'query/disconnect',
            checkActiveConn: 'query/checkActiveConn',
            fetchDbList: 'query/fetchDbList',
            reloadTreeNodes: 'query/reloadTreeNodes',
            updateActiveDb: 'query/updateActiveDb',
        }),
        ...mapMutations({
            SET_CURR_CNCT_RESOURCE: 'query/SET_CURR_CNCT_RESOURCE',
        }),
        async initialFetch() {
            await this.reloadTreeNodes()
            await this.updateActiveDb()
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
        async handleOpenConn(body) {
            await this.openConnect(body)
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
</style>
