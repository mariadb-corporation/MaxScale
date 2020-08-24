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
                                :disabled="serviceState === 'Stopped'"
                                v-on="on"
                                @click="actionHandle('stop')"
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
                                :disabled="serviceState === 'Started'"
                                v-on="on"
                                @click="actionHandle('start')"
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
                            <v-btn text v-on="on" @click="actionHandle('destroy')">
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
                :item="current_service"
                :onSave="confirmSave"
                :onClose="() => (showConfirmDialog = false)"
                :onCancel="() => (showConfirmDialog = false)"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.serviceStateIcon(serviceState)"
            >
                status
            </icon-sprite-sheet>
            <span class="color text-navigation body-2">
                {{ serviceState }}
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
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState } from 'vuex'

export default {
    name: 'page-header',
    props: {
        onEditSucceeded: { type: Function, required: true },
    },
    data() {
        return {
            showConfirmDialog: false,
            dialogTitle: '',
            dialogType: 'destroy',
        }
    },
    computed: {
        ...mapState({
            current_service: state => state.service.current_service,
        }),
        serviceState: function() {
            return this.current_service.attributes.state
        },
    },
    methods: {
        ...mapActions('service', ['destroyService', 'stopOrStartService']),
        async confirmSave() {
            await this.performAsyncLoadingAction(this.dialogType)
        },

        async performAsyncLoadingAction(mode) {
            const { id } = this.current_service
            switch (mode) {
                case 'destroy':
                    await this.destroyService(id)
                    this.showConfirmDialog = false
                    this.$router.go(-1)
                    break
                default:
                    await this.stopOrStartService({
                        id,
                        mode,
                        callback: this.onEditSucceeded,
                    })
                    this.showConfirmDialog = false
            }
        },

        actionHandle(type) {
            this.dialogType = type
            this.dialogTitle = `${this.$t(type)} ${this.$tc('services', 1)}`
            this.showConfirmDialog = true
        },
    },
}
</script>
