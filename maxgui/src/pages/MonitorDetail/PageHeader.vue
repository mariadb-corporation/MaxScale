<template>
    <details-page-title>
        <template v-slot:setting-menu>
            <details-icon-group-wrapper multiIcons>
                <template v-slot:body>
                    <v-tooltip
                        v-for="op in [
                            monitorOps[MONITOR_OP_TYPES.STOP],
                            monitorOps[MONITOR_OP_TYPES.START],
                        ]"
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
            <details-icon-group-wrapper>
                <template v-slot:body>
                    <v-tooltip
                        v-for="op in [monitorOps[MONITOR_OP_TYPES.DESTROY]]"
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
        </template>
        <template v-slot:append>
            <confirm-dialog
                v-model="isConfDlgOpened"
                :title="dialogTitle"
                :type="dialogType"
                :item="currentMonitor"
                :onSave="confirmSave"
            />
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.monitorStateIcon(currState)"
            >
                status
            </icon-sprite-sheet>
            <span class="resource-state color text-navigation text-body-2">
                {{ currState }}
            </span>
            <span class="color text-field-text text-body-2">
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
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState, mapGetters } from 'vuex'
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
            opParams: '',
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({ MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES }),
        ...mapGetters({ getMonitorOps: 'monitor/getMonitorOps' }),
        currState() {
            const { attributes: { state = 'null' } = {} } = this.currentMonitor
            return state
        },
        getModule() {
            const { attributes: { module: monitorModule = null } = {} } = this.currentMonitor
            return monitorModule
        },
        monitorOps() {
            return this.getMonitorOps({ currState: this.currState, scope: this })
        },
    },
    methods: {
        ...mapActions('monitor', ['manipulateMonitor']),
        async confirmSave() {
            await this.handleMonitorOp(this.dialogType)
        },

        async handleMonitorOp(type) {
            const { id } = this.currentMonitor
            const { STOP, START, DESTROY } = this.MONITOR_OP_TYPES
            let payload = {
                id,
                type,
                opParams: this.opParams,
            }
            switch (type) {
                case DESTROY:
                    payload.callback = this.goBack
                    break
                case STOP:
                case START:
                    payload.callback = this.onEditSucceeded
            }
            await this.manipulateMonitor(payload)
        },

        handleClick({ type, text, params }) {
            this.dialogType = type
            this.dialogTitle = text
            this.opParams = params
            this.isConfDlgOpened = true
        },
    },
}
</script>
