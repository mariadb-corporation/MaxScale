<template>
    <details-page-title>
        <template v-slot:setting-menu>
            <details-icon-group-wrapper>
                <template v-slot:body>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                class="delete-btn"
                                text
                                color="error"
                                v-on="on"
                                @click="handleDelete"
                            >
                                <v-icon size="18">
                                    $vuetify.icons.mxs_delete
                                </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ $t('destroy') }} {{ $tc('listeners', 1) }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
        </template>
        <template v-slot:append>
            <portal to="page-header--right">
                <global-search class="d-inline-block" />
                <create-resource class="ml-4 d-inline-block" />
            </portal>
            <mxs-conf-dlg
                v-model="isConfDlgOpened"
                :title="dialogTitle"
                :saveText="dialogType"
                :type="dialogType"
                :item="currentListener"
                :onSave="confirmSave"
            />
            <icon-sprite-sheet
                size="16"
                class="listener-state-icon mr-1"
                :frame="$helpers.listenerStateIcon(currentListener.attributes.state)"
            >
                listeners
            </icon-sprite-sheet>
            <span class="resource-state mxs-color-helper text-navigation text-body-2">
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
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions } from 'vuex'
import goBack from '@share/mixins/goBack'

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
            isConfDlgOpened: false,
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
            this.isConfDlgOpened = true
        },
    },
}
</script>
