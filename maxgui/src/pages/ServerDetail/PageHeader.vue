<template>
    <details-page-title>
        <template v-slot:setting-menu>
            <details-icon-group-wrapper multiIcons>
                <template v-slot:body>
                    <v-tooltip
                        v-for="op in [serverOps.maintain, serverOps.clear]"
                        :key="op.text"
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                :class="`${op.type}-btn`"
                                text
                                :color="op.color"
                                :disabled="op.disabled"
                                v-on="on"
                                @click="handleClick(op)"
                            >
                                <v-icon :size="op.iconSize"> {{ op.icon }} </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ op.text }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
            <details-icon-group-wrapper
                v-for="op in [serverOps.drain, serverOps.delete]"
                :key="op.text"
            >
                <template v-slot:body>
                    <v-tooltip
                        bottom
                        transition="slide-y-transition"
                        content-class="shadow-drop color text-navigation py-1 px-4"
                    >
                        <template v-slot:activator="{ on }">
                            <v-btn
                                :class="`${op.type}-btn`"
                                text
                                :color="op.color"
                                :disabled="op.disabled"
                                v-on="on"
                                @click="handleClick(op)"
                            >
                                <v-icon :size="op.iconSize"> {{ op.icon }} </v-icon>
                            </v-btn>
                        </template>
                        <span>{{ op.text }} </span>
                    </v-tooltip>
                </template>
            </details-icon-group-wrapper>
        </template>
        <template v-slot:append>
            <confirm-dialog
                v-model="isConfDlgOpened"
                :title="dialogTitle"
                :type="dialogType"
                :smallInfo="smallInfo"
                :item="currentServer"
                :onSave="confirmSave"
            >
                <template v-if="dialogType === 'maintain'" v-slot:body-append>
                    <v-checkbox
                        v-model="forceClosing"
                        class="small mt-2 mb-4"
                        :label="$t('forceClosing')"
                        color="primary"
                        hide-details
                    />
                </template>
            </confirm-dialog>
            <icon-sprite-sheet size="13" class="status-icon mr-1" :frame="stateIconFrame">
                status
            </icon-sprite-sheet>
            <span class="color text-navigation text-body-2 server-healthy">
                {{ serverHealthy }}
            </span>
            <span v-if="version_string" class="color text-field-text text-body-2">
                |
                <span class="version-string">{{ $t('version') }} {{ version_string }}</span>
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
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapGetters } from 'vuex'
import goBack from 'mixins/goBack'

export default {
    name: 'page-header',
    mixins: [goBack],
    props: {
        onEditSucceeded: { type: Function, required: true },
        currentServer: { type: Object, required: true },
    },
    data() {
        return {
            dialogTitle: '',
            dialogType: 'delete',
            smallInfo: '',
            opParams: '',
            forceClosing: false,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapGetters({
            getCurrStateMode: 'server/getCurrStateMode',
            getServerOps: 'server/getServerOps',
        }),
        version_string() {
            return this.currentServer.attributes.version_string
        },
        serverState() {
            return this.currentServer.attributes.state
        },
        /**
         * @returns {Number} returns a number: 0,1,2
         */
        stateIconFrame() {
            return this.$help.serverStateIcon(this.serverState)
        },
        serverHealthy() {
            switch (this.stateIconFrame) {
                case 0:
                    return this.$t('unHealthy')
                case 1:
                    return this.$t('healthy')
                default:
                    return this.$t('warning')
            }
        },
        currStateMode() {
            return this.getCurrStateMode(this.serverState)
        },
        serverOps() {
            return this.getServerOps({ currStateMode: this.currStateMode, scope: this })
        },
    },
    methods: {
        ...mapActions('server', ['destroyServer', 'setOrClearServerState']),
        handleClick({ type, text, info, params }) {
            this.dialogType = type
            this.dialogTitle = text
            this.opParams = params
            this.smallInfo = info
            this.isConfDlgOpened = true
        },
        async confirmSave() {
            switch (this.dialogType) {
                case 'delete':
                    await this.destroyServer(this.currentServer.id)
                    this.goBack()
                    break
                case 'drain':
                case 'clear':
                case 'maintain':
                    await this.setOrClearServerState({
                        id: this.currentServer.id,
                        opParams: this.opParams,
                        type: this.dialogType,
                        callback: this.onEditSucceeded,
                        forceClosing: this.forceClosing,
                    })
                    break
            }
        },
    },
}
</script>
