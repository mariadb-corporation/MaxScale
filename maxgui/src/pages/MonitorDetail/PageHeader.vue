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
                                :disabled="getState === 'Stopped'"
                                v-on="on"
                                @click="actionHandle('stop')"
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
                                class="start-btn"
                                text
                                :disabled="getState === 'Running'"
                                v-on="on"
                                @click="actionHandle('start')"
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
                        <span>{{ $t('destroy') }} {{ $tc('monitors', 1) }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
        </template>
        <template v-slot:append>
            <confirm-dialog
                ref="monitorConfirmDialog"
                :title="dialogTitle"
                :type="dialogType"
                :item="currentMonitor"
                :onSave="confirmSave"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.monitorStateIcon(getState)"
            >
                status
            </icon-sprite-sheet>
            <span class="resource-state color text-navigation body-2">
                {{ getState }}
            </span>
            <span class="color text-field-text body-2">
                |
                <span class="resource-module">{{ getModule }}</span>
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
 * Change Date: 2024-11-26
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
        currentMonitor: { type: Object, required: true },
        onEditSucceeded: { type: Function, required: true },
    },
    data() {
        return {
            dialogTitle: '',
            dialogType: 'destroy',
        }
    },
    computed: {
        getState: function() {
            const { attributes: { state = 'null' } = {} } = this.currentMonitor
            return state
        },
        getModule: function() {
            const { attributes: { module: monitorModule = null } = {} } = this.currentMonitor
            return monitorModule
        },
    },
    methods: {
        ...mapActions('monitor', ['manipulateMonitor']),
        async confirmSave() {
            await this.performAsyncLoadingAction(this.dialogType)
        },

        async performAsyncLoadingAction(mode) {
            const { id } = this.currentMonitor
            switch (mode) {
                case 'destroy':
                    await this.manipulateMonitor({
                        id,
                        mode,
                    })

                    this.goBack()
                    break
                default:
                    await this.manipulateMonitor({
                        id,
                        mode,
                        callback: this.onEditSucceeded,
                    })
            }
        },

        actionHandle(type) {
            this.dialogType = type
            this.dialogTitle = `${this.$t(type)} ${this.$tc('monitors', 1)}`
            this.$refs.monitorConfirmDialog.open()
        },
    },
}
</script>
