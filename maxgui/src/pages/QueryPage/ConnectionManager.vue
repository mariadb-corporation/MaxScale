<template>
    <div>
        <v-btn
            outlined
            width="220"
            class="text-none px-2 font-weight-regular"
            depressed
            small
            color="accent-dark"
            @click="curr_cnct_resource ? null : openConnDialog()"
        >
            <div
                id="curr_cnct_resource"
                class="d-flex align-center"
                :style="{ maxWidth: `${curr_cnct_resource ? 162 : 198}px` }"
            >
                <v-icon v-if="curr_cnct_resource" class="mr-2" size="16" color="accent-dark">
                    $vuetify.icons.server
                </v-icon>
                <div class="text-truncate">
                    {{ curr_cnct_resource ? curr_cnct_resource.name : $t('openConnection') }}
                </div>
            </div>
            <v-tooltip
                v-if="curr_cnct_resource"
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
        </v-btn>
        <v-tooltip
            v-if="curr_cnct_resource"
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
            nudge-right="20px"
            activator="#curr_cnct_resource"
        >
            <span>{{ $t('connectedTo') }}: {{ curr_cnct_resource.name }} </span>
        </v-tooltip>

        <connection-dialog v-model="isConnDialogOpened" :handleSave="handleOpenConn" />
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
        }
    },
    computed: {
        ...mapState({
            checking_active_conn: state => state.query.checking_active_conn,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_conn_state: state => state.query.active_conn_state,
        }),
    },
    watch: {
        checking_active_conn() {
            this.handleAutoOpenDialog()
        },
    },
    mounted() {
        this.handleAutoOpenDialog()
    },
    methods: {
        ...mapActions({
            openConnect: 'query/openConnect',
            disconnect: 'query/disconnect',
        }),
        handleAutoOpenDialog() {
            if (!this.checking_active_conn && !this.active_conn_state) this.openConnDialog()
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
