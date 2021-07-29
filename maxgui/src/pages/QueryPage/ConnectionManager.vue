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
            :placeholder="$t('selectConnection')"
        >
            <template v-slot:selection="{ item }">
                <div class="d-flex align-center pl-1">
                    <v-icon class="mr-2" size="16" color="accent-dark">
                        $vuetify.icons.server
                    </v-icon>
                    <truncate-string :text="item" :maxWidth="145" :nudgeLeft="32" />
                </div>
            </template>
            <template v-slot:item="{ item, on, attrs }">
                <div
                    class="v-list-item__title d-flex align-center flex-row flex-grow-1"
                    v-bind="attrs"
                    v-on="on"
                >
                    <div
                        v-if="item === newConnOption"
                        class="text-decoration-underline color text-primary"
                    >
                        {{ item }}
                    </div>
                    <template v-else>
                        <v-icon class="mr-2" size="16" color="accent-dark">
                            $vuetify.icons.server
                        </v-icon>
                        <truncate-string :text="item" :maxWidth="135" :nudgeLeft="32" />
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
                                    @click.prevent="() => $refs.confirmDialog.open()"
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
            v-if="curr_cnct_resource"
            ref="confirmDialog"
            :title="$t('disconnectConn')"
            type="disconnect"
            :item="{ id: curr_cnct_resource.name }"
            :onSave="() => disconnect({ showSnackbar: true })"
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
import { mapActions, mapState } from 'vuex'
import ConnectionDialog from './ConnectionDialog'
export default {
    name: 'connection-manager',
    components: {
        ConnectionDialog,
    },
    data() {
        return {
            isConnDialogOpened: false,
            chosenConn: '',
            newConnOption: this.$t('newConnection'),
        }
    },
    computed: {
        ...mapState({
            checking_active_conn: state => state.query.checking_active_conn,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_conn_state: state => state.query.active_conn_state,
        }),
        connOptions() {
            let options = [this.newConnOption]
            if (this.curr_cnct_resource) options.unshift(this.curr_cnct_resource.name)
            return options
        },
    },
    watch: {
        checking_active_conn(v) {
            //After finish checking checking_active_conn, auto open dialog if there is no active connection
            if (!v && !this.active_conn_state) this.openConnDialog()
            else this.assignActiveConn()
        },
        chosenConn(v) {
            if (v === this.newConnOption) this.openConnDialog()
            else this.assignActiveConn()
        },
        curr_cnct_resource(v) {
            if (v) this.chosenConn = v.name
        },
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
            disconnect: 'query/disconnect',
        }),
        assignActiveConn() {
            if (this.curr_cnct_resource) this.chosenConn = this.curr_cnct_resource.name
            else this.chosenConn = ''
        },
        openConnDialog() {
            this.isConnDialogOpened = true
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
