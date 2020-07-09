<template>
    <details-page-title>
        <template v-slot:setting-menu>
            <details-icon-group-wrapper multiIcons>
                <template v-slot:body>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                text
                                :disabled="serverState === 'maintenance'"
                                v-on="on"
                                @click="handleClick('maintenance')"
                            >
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.paused
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('maintain') }} {{ $tc('servers', 1) }} </span>
                    </v-tooltip>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                text
                                :disabled="
                                    serverState !== 'maintenance' && serverState !== 'drained'
                                "
                                v-on="on"
                                @click="handleClick('clear')"
                            >
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.restart
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('clear') }} {{ $t('currentServerState') }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
            <details-icon-group-wrapper>
                <template v-slot:body>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn text v-on="on" @click="handleClick('drain')">
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.drain
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('drain') }} {{ $tc('servers', 1) }}</span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
            <details-icon-group-wrapper>
                <template v-slot:body>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn text v-on="on" @click="handleClick('delete')">
                                <v-icon size="22" color="error">
                                    $vuetify.icons.unlink
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('unlink') }} {{ $tc('servers', 1) }}</span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
        </template>
        <template v-slot:append>
            <confirm-dialog
                v-model="showConfirmDialog"
                :title="dialogTitle"
                :type="dialogType"
                :smallInfo="smallInfo ? $t(`info.${smallInfo}`) : ''"
                :item="currentServer"
                :onSave="confirmSave"
                :onClose="() => (showConfirmDialog = false)"
                :onCancel="() => (showConfirmDialog = false)"
            >
                <template v-if="state === 'maintenance' && mode === 'set'" v-slot:body-append>
                    <v-checkbox
                        v-model="forceClosing"
                        class="small mt-2 mb-4"
                        :label="$t('forceClosing')"
                        color="primary"
                        hide-details
                    />
                </template>
            </confirm-dialog>
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.serverStateIcon(currentServer.attributes.state)"
            >
                status
            </icon-sprite-sheet>
            <span class="color text-navigation body-2">
                {{ serverHealthy }}
            </span>
            <span
                v-if="!$help.lodash.isEmpty(currentServer.attributes.version_string)"
                class="color text-field-text body-2"
            >
                |
                <span>{{ $t('version') }} {{ currentServer.attributes.version_string }}</span>
            </span>
        </template>
    </details-page-title>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions } from 'vuex'

export default {
    name: 'page-header',
    props: {
        currentServer: { type: Object, required: true },
        onEditSucceeded: { type: Function, required: true },
    },
    data() {
        return {
            showConfirmDialog: false,
            dialogTitle: '',
            dialogType: 'unlink',
            smallInfo: 'serviceDelete',
            mode: 'set', //set or clear
            state: '',
            forceClosing: false,
        }
    },
    computed: {
        serverHealthy: function() {
            switch (this.$help.serverStateIcon(this.currentServer.attributes.state)) {
                case 0:
                    return 'Unhealthy'
                case 1:
                    return 'Healthy'
                default:
                    return 'Warning'
            }
        },
        serverState: function() {
            let currentState = this.currentServer.attributes.state.toLowerCase()
            if (currentState.indexOf(',') > 0) {
                currentState = currentState.slice(0, currentState.indexOf(','))
            }
            return currentState
        },
    },
    methods: {
        ...mapActions('server', ['destroyServer', 'setOrClearServerState']),
        handleClick(mode) {
            switch (mode) {
                case 'delete':
                    {
                        this.mode = 'delete'
                        this.dialogType = 'unlink'
                        this.dialogTitle = `${this.$t('unlink')} ${this.$tc('servers', 1)}`
                        this.smallInfo = 'serverUnlink'
                    }
                    break
                case 'drain':
                    {
                        this.mode = 'set'
                        this.dialogType = 'drain'
                        this.state = 'drain'
                        this.dialogTitle = `${this.$t('drain')} ${this.$tc('servers', 1)}`
                        this.smallInfo = 'serverDrain'
                    }
                    break
                case 'clear':
                    {
                        this.mode = 'clear'
                        let currentState = this.serverState

                        this.state = currentState === 'drained' ? 'drain' : currentState
                        this.dialogType = 'clear'
                        this.dialogTitle = `${this.$t('clear')} ${this.$tc('servers', 1)}`
                        this.smallInfo = 'serverClear'
                    }
                    break
                case 'maintenance':
                    {
                        this.mode = 'set'
                        this.state = 'maintenance'
                        this.dialogType = 'maintain'
                        this.dialogTitle = `${this.$t('maintain')} ${this.$tc('servers', 1)}`
                        this.smallInfo = 'serverMaintenance'
                    }
                    break
            }

            this.showConfirmDialog = true
        },

        async confirmSave() {
            switch (this.mode) {
                case 'delete':
                    await this.destroyServer(this.currentServer.id)
                    this.showConfirmDialog = false
                    this.$router.go(-1)
                    break
                case 'set':
                case 'clear':
                    await this.performAsyncLoadingAction()
                    break
            }
        },
        async performAsyncLoadingAction() {
            const self = this
            self.showConfirmDialog = false
            let payload = {
                id: self.currentServer.id,
                state: self.state,
                mode: self.mode,
                callback: self.onEditSucceeded,
            }
            if (this.forceClosing) {
                payload.forceClosing = true
            }
            await self.setOrClearServerState(payload)
        },
    },
}
</script>
