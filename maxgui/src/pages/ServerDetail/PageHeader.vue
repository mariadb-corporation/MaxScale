<template>
    <details-page-title>
        <template v-slot:setting-menu>
            <details-icon-group-wrapper multiIcons>
                <template v-slot:body>
                    <mxs-tooltip-btn
                        v-for="op in [
                            serverOps[SERVER_OP_TYPES.MAINTAIN],
                            serverOps[SERVER_OP_TYPES.CLEAR],
                        ]"
                        :key="op.text"
                        :tooltipProps="{ bottom: true }"
                        :btnClass="`${op.type}-btn`"
                        text
                        :color="op.color"
                        :disabled="op.disabled"
                        @click="handleClick(op)"
                    >
                        <template v-slot:btn-content>
                            <v-icon :size="op.iconSize"> {{ op.icon }} </v-icon>
                        </template>
                        {{ op.text }}
                    </mxs-tooltip-btn>
                </template>
            </details-icon-group-wrapper>
            <details-icon-group-wrapper
                v-for="op in [serverOps[SERVER_OP_TYPES.DRAIN], serverOps[SERVER_OP_TYPES.DELETE]]"
                :key="op.text"
            >
                <template v-slot:body>
                    <mxs-tooltip-btn
                        :tooltipProps="{ bottom: true }"
                        :btnClass="`${op.type}-btn`"
                        text
                        :color="op.color"
                        :disabled="op.disabled"
                        @click="handleClick(op)"
                    >
                        <template v-slot:btn-content>
                            <v-icon :size="op.iconSize"> {{ op.icon }} </v-icon>
                        </template>
                        {{ op.text }}
                    </mxs-tooltip-btn>
                </template>
            </details-icon-group-wrapper>
        </template>
        <template v-slot:append>
            <portal to="page-header--right">
                <refresh-rate v-model="refreshRate" v-on="$listeners" />
                <global-search class="ml-4 d-inline-block" />
                <create-mxs-obj
                    class="ml-4 d-inline-block"
                    :defRelationshipObj="{
                        id: $route.params.id,
                        type: MXS_OBJ_TYPES.SERVERS,
                    }"
                />
            </portal>
            <confirm-dlg
                v-model="isConfDlgOpened"
                :title="dialogTitle"
                :saveText="confDlgSaveTxt"
                :type="dialogType"
                :item="currentServer"
                :smallInfo="smallInfo"
                :onSave="confirmSave"
            >
                <template v-if="dialogType === 'maintain'" v-slot:body-append>
                    <v-checkbox
                        v-model="forceClosing"
                        class="v-checkbox--mariadb mt-2 mb-4"
                        :label="$mxs_t('forceClosing')"
                        color="primary"
                        hide-details
                        dense
                    />
                </template>
            </confirm-dlg>
            <status-icon
                size="16"
                class="server-state-icon mr-1"
                :type="MXS_OBJ_TYPES.SERVERS"
                :value="serverState"
            />

            <span class="mxs-color-helper text-navigation text-body-2 server-healthy">
                {{ serverHealthy }}
            </span>
            <span v-if="version_string" class="mxs-color-helper text-grayed-out text-body-2">
                |
                <span class="version-string">{{ $mxs_t('version') }} {{ version_string }}</span>
            </span>
        </template>
    </details-page-title>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapGetters } from 'vuex'
import goBack from '@src/mixins/goBack'
import refreshRate from '@src/mixins/refreshRate'
import { MXS_OBJ_TYPES } from '@share/constants'
import { SERVER_OP_TYPES } from '@src/constants'
import statusIconHelpers from '@src/utils/statusIconHelpers'

export default {
    name: 'page-header',
    mixins: [goBack, refreshRate],
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
            return statusIconHelpers[MXS_OBJ_TYPES.SERVERS](this.serverState)
        },
        serverHealthy() {
            switch (this.stateIconFrame) {
                case 0:
                    return this.$mxs_t('unHealthy')
                case 1:
                    return this.$mxs_t('healthy')
                default:
                    return this.$mxs_t('maintenance')
            }
        },
        currStateMode() {
            return this.getCurrStateMode(this.serverState)
        },
        serverOps() {
            return this.getServerOps({ currStateMode: this.currStateMode, scope: this })
        },
        confDlgSaveTxt() {
            const { MAINTAIN } = this.SERVER_OP_TYPES
            switch (this.dialogType) {
                case MAINTAIN:
                    return 'set'
                default:
                    return this.dialogType
            }
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
        this.SERVER_OP_TYPES = SERVER_OP_TYPES
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
            const { MAINTAIN, CLEAR, DRAIN, DELETE } = this.SERVER_OP_TYPES
            switch (this.dialogType) {
                case DELETE:
                    await this.destroyServer(this.currentServer.id)
                    this.goBack()
                    break
                case DRAIN:
                case CLEAR:
                case MAINTAIN:
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
