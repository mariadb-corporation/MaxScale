<template>
    <monitor-page-header
        :targetMonitor="currentMonitor"
        @on-choose-op="onChooseOp"
        v-on="$listeners"
    >
        <template v-slot:page-title="{ pageId }">
            <router-link :to="`/visualization/clusters/${pageId}`" class="rsrc-link">
                {{ pageId }}
            </router-link>
            <confirm-dialog
                v-model="confDlg.isOpened"
                :title="confDlg.title"
                :type="confDlg.type"
                :saveText="confDlgSaveTxt"
                :item="confDlg.targetNode"
                :smallInfo="confDlg.smallInfo"
                :onSave="onConfirm"
            />
        </template>
    </monitor-page-header>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapActions, mapState } from 'vuex'
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
            // states for confirm-dialog
            confDlg: {
                opType: '',
                isOpened: false,
                title: '',
                type: '',
                targetNode: null,
                smallInfo: '',
            },
        }
    },
    computed: {
        ...mapState({
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
        }),
        getModule() {
            const { attributes: { module: monitorModule = null } = {} } = this.currentMonitor
            return monitorModule
        },
        confDlgSaveTxt() {
            const { RESET_REP, RELEASE_LOCKS, FAILOVER } = this.MONITOR_OP_TYPES
            switch (this.confDlg.type) {
                case RESET_REP:
                    return 'reset'
                case RELEASE_LOCKS:
                    return 'release'
                case FAILOVER:
                    return 'perform'
                default:
                    return this.confDlg.type
            }
        },
    },
    methods: {
        ...mapActions('monitor', ['manipulateMonitor']),
        async onConfirm() {
            const {
                STOP,
                START,
                DESTROY,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
            } = this.MONITOR_OP_TYPES
            let payload = {
                id: this.currentMonitor.id,
                type: this.confDlg.opType,
                callback: this.onEditSucceeded,
            }
            switch (this.confDlg.opType) {
                case RESET_REP:
                case RELEASE_LOCKS:
                case FAILOVER: {
                    await this.manipulateMonitor({
                        ...payload,
                        opParams: { moduleType: this.getModule, params: '' },
                    })
                    break
                }
                case STOP:
                case START:
                    await this.manipulateMonitor({ ...payload, opParams: this.confDlg.opParams })
                    break
                case DESTROY:
                    await this.manipulateMonitor({ ...payload, callback: this.goBack })
            }
        },
        onChooseOp({ type, text, info, params }) {
            this.confDlg = {
                ...this.confDlg,
                type,
                opType: type,
                title: text,
                opParams: params,
                targetNode: { id: this.currentMonitor.id },
                smallInfo: info,
                isOpened: true,
            }
        },
    },
}
</script>
