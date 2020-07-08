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
                                :disabled="currentService.attributes.state === 'Stopped'"
                                v-on="on"
                                @click="handleStop"
                            >
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.stopped
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('stop') }} {{ $tc('services', 1) }} </span>
                    </v-tooltip>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                text
                                :disabled="currentService.attributes.state === 'Started'"
                                v-on="on"
                                @click="handleStart"
                            >
                                <v-icon size="22" color="primary">
                                    $vuetify.icons.running
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('start') }} {{ $tc('services', 1) }} </span>
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
                        <span>{{ $t('destroy') }} {{ $tc('services', 1) }} </span>
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
                :item="currentService"
                :onSave="confirmSave"
                :onClose="() => (showConfirmDialog = false)"
                :onCancel="() => (showConfirmDialog = false)"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.serviceStateIcon(currentService.attributes.state)"
            >
                status
            </icon-sprite-sheet>
            <span class="color text-navigation body-2">
                {{ currentService.attributes.state }}
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
        currentService: { type: Object, required: true },
        onEditSucceeded: { type: Function, required: true },
    },
    data() {
        return {
            showConfirmDialog: false,
            dialogTitle: '',
            dialogType: 'destroy',
            smallInfo: 'serviceDelete',
        }
    },

    methods: {
        ...mapActions('service', ['destroyService', 'stopOrStartService']),
        async confirmSave() {
            await this.performAsyncLoadingAction(this.dialogType)
        },

        async performAsyncLoadingAction(type) {
            let self = this
            if (type === 'destroy') {
                await self.destroyService(self.currentService.id)
                self.showConfirmDialog = false
                self.$router.go(-1)
            } else {
                await self.stopOrStartService({
                    id: self.currentService.id,
                    mode: type,
                    callback: self.onEditSucceeded,
                })
                self.showConfirmDialog = false
            }
        },

        handleDelete() {
            this.dialogType = 'destroy'
            this.dialogTitle = `${this.$t('destroy')} ${this.$tc('services', 1)}`
            this.smallInfo = 'serviceDelete'
            this.showConfirmDialog = true
        },
        handleStop() {
            this.dialogType = 'stop'
            this.dialogTitle = `${this.$t('stop')} ${this.$tc('services', 1)}`
            this.smallInfo = ''
            this.showConfirmDialog = true
        },
        handleStart() {
            this.dialogType = 'start'
            this.dialogTitle = `${this.$t('start')} ${this.$tc('services', 1)}`
            this.smallInfo = ''
            this.showConfirmDialog = true
        },
    },
}
</script>
