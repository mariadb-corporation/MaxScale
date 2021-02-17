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
                                class="stop-btn"
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
                                class="start-btn"
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
                            <v-btn
                                class="delete-btn"
                                text
                                v-on="on"
                                @click="actionHandle('destroy')"
                            >
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
                :item="currentService"
                :onSave="confirmSave"
                :onClose="handleClose"
                :onCancel="handleClose"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.serviceStateIcon(serviceState)"
            >
                status
            </icon-sprite-sheet>
            <span class="service-state color text-navigation body-2">
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
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions } from 'vuex'
import goBack from 'mixins/goBack'

export default {
    name: 'page-header',
    mixins: [goBack],
    props: {
        onEditSucceeded: { type: Function, required: true },
        currentService: { type: Object, required: true },
    },
    data() {
        return {
            showConfirmDialog: false,
            dialogTitle: '',
            dialogType: 'destroy',
        }
    },
    computed: {
        serviceState: function() {
            return this.currentService.attributes.state
        },
    },
    methods: {
        ...mapActions('service', ['destroyService', 'stopOrStartService']),
        handleClose() {
            this.showConfirmDialog = false
        },

        actionHandle(type) {
            this.dialogType = type
            this.dialogTitle = `${this.$t(type)} ${this.$tc('services', 1)}`
            this.showConfirmDialog = true
        },

        async confirmSave() {
            await this.performAsyncLoadingAction(this.dialogType)
        },

        async performAsyncLoadingAction(mode) {
            const { id } = this.currentService
            switch (mode) {
                case 'destroy':
                    await this.destroyService(id)
                    this.showConfirmDialog = false
                    this.goBack()
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
    },
}
</script>
