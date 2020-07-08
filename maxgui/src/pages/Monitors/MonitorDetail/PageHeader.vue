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
                                :disabled="currentMonitor.attributes.state === 'Stopped'"
                                v-on="on"
                                @click="handleStop"
                            >
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.stopped
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('stop') }} {{ $tc('monitors', 1) }} </span>
                    </v-tooltip>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                text
                                :disabled="currentMonitor.attributes.state === 'Running'"
                                v-on="on"
                                @click="handleStart"
                            >
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.running
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('start') }} {{ $tc('monitors', 1) }} </span>
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
                            <v-btn text v-on="on" @click="handleDelete">
                                <v-icon size="18" color="error">
                                    $vuetify.icons.delete
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('destroy') }} {{ $tc('monitors', 1) }} </span>
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
                :item="currentMonitor"
                :onSave="confirmSave"
                :onClose="() => (showConfirmDialog = false)"
                :onCancel="() => (showConfirmDialog = false)"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.monitorStateIcon(currentMonitor.attributes.state)"
            >
                status
            </icon-sprite-sheet>
            <span class="color text-navigation body-2">
                {{ currentMonitor.attributes.state }}
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
        currentMonitor: { type: Object, required: true },
        onEditSucceeded: { type: Function, required: true },
    },
    data() {
        return {
            showConfirmDialog: false,
            dialogTitle: '',
            dialogType: 'destroy',
            smallInfo: 'monitorDelete',
        }
    },

    methods: {
        ...mapActions('monitor', ['monitorManipulate']),
        async confirmSave() {
            await this.performAsyncLoadingAction(this.dialogType)
        },

        async performAsyncLoadingAction(type) {
            let self = this
            switch (type) {
                case 'destroy':
                    await self.monitorManipulate({
                        id: self.currentMonitor.id,
                        mode: type,
                    })
                    self.showConfirmDialog = false
                    self.$router.go(-1)
                    break
                default:
                    await self.monitorManipulate({
                        id: self.currentMonitor.id,
                        mode: type,
                        callback: self.onEditSucceeded,
                    })
                    self.showConfirmDialog = false
            }
        },

        handleDelete() {
            this.dialogType = 'destroy'
            this.dialogTitle = `${this.$t('destroy')} ${this.$tc('monitors', 1)}`
            this.smallInfo = 'monitorDelete'
            this.showConfirmDialog = true
        },
        handleStop() {
            this.dialogType = 'stop'
            this.dialogTitle = `${this.$t('stop')} ${this.$tc('monitors', 1)}`
            this.smallInfo = ''
            this.showConfirmDialog = true
        },
        handleStart() {
            this.dialogType = 'start'
            this.dialogTitle = `${this.$t('start')} ${this.$tc('monitors', 1)}`
            this.smallInfo = ''
            this.showConfirmDialog = true
        },
    },
}
</script>
