<template>
    <details-page-title>
        <template v-slot:setting-menu>
            <details-icon-group-wrapper>
                <template v-slot:body>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn class="delete-btn" text v-on="on" @click="handleDelete">
                                <v-icon size="18" color="error">
                                    $vuetify.icons.delete
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('destroy') }} {{ $tc('listeners', 1) }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
        </template>
        <template v-slot:append>
            <confirm-dialog
                ref="listenerConfirmDialog"
                :title="dialogTitle"
                :type="dialogType"
                :item="currentListener"
                :onSave="confirmSave"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.listenerStateIcon(currentListener.attributes.state)"
            >
                status
            </icon-sprite-sheet>
            <span class="resource-state color text-navigation body-2">
                {{ currentListener.attributes.state }}
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
 * Change Date: 2024-10-14
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
        currentListener: { type: Object, required: true },
    },
    data() {
        return {
            dialogTitle: '',
            dialogType: 'destroy',
        }
    },

    methods: {
        ...mapActions('listener', ['destroyListener']),
        async confirmSave() {
            await this.performAsyncLoadingAction(this.dialogType)
        },

        async performAsyncLoadingAction(type) {
            switch (type) {
                case 'destroy':
                    await this.destroyListener(this.currentListener.id)
                    this.goBack()
                    break
                default:
                    null
            }
        },

        handleDelete() {
            this.dialogType = 'destroy'
            this.dialogTitle = `${this.$t('destroy')} ${this.$tc('listeners', 1)}`
            this.$refs.listenerConfirmDialog.open()
        },
    },
}
</script>
