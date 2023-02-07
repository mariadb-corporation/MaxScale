<template>
    <mxs-dlg
        v-model="showReconnDialog"
        :title="$mxs_t('errors.serverHasGoneAway')"
        minBodyWidth="624px"
        :onSave="handleReconnect"
        cancelText="disconnect"
        saveText="reconnect"
        :showCloseIcon="false"
        @on-cancel="deleteConns"
    >
        <template v-slot:form-body>
            <table v-if="showReconnDialog" class="tbl-code lost-conn-tbl pa-4">
                <tbody v-for="(v, i) in lostConns" :key="i">
                    <tr>
                        <td class="font-weight-bold">{{ $mxs_tc('servers', 1) }}</td>
                        <td>{{ v.meta.name }}</td>
                    </tr>
                    <tr v-for="(errValue, errField) in v.lost_cnn_err" :key="errField">
                        <td class="font-weight-bold">{{ errField }}</td>
                        <td>{{ errValue }}</td>
                    </tr>
                </tbody>
            </table>
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import EtlTask from '@wsModels/EtlTask'
import { mapState, mapActions } from 'vuex'

export default {
    name: 'reconn-dlg-ctr',
    computed: {
        ...mapState({
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
        }),
        isActiveQueryEditorWke() {
            return Boolean(Worksheet.getters('getActiveQueryTabId'))
        },
        isActiveEtlWke() {
            return Boolean(Worksheet.getters('getActiveEtlTaskId'))
        },
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTask')
        },
        activeWkeConn() {
            return QueryConn.getters('getActiveWkeConn')
        },
        activeConns() {
            if (this.isActiveQueryEditorWke)
                return [this.activeWkeConn, QueryConn.getters('getActiveQueryTabConn')]
            if (this.isActiveEtlWke) return QueryConn.getters('getActiveEtlConns')
            return []
        },
        lostConns() {
            return this.activeConns.reduce((arr, c) => {
                if (this.$typy(c, 'lost_cnn_err.message').safeString) arr.push(c)
                return arr
            }, [])
        },
        lostConnIds() {
            return this.lostConns.map(c => c.id)
        },
        isWkeConnLost() {
            return this.lostConnIds.includes(this.activeWkeConn.id)
        },
        connIdsToBeReconnected() {
            let ids = this.lostConnIds
            /**
             * The worksheet connection is normally not included in 'lostConns'
             * since the `lost_cnn_err` is retrieved if the worksheet connection
             * is used for querying. The worksheet connection, on the other hand,
             * is solely utilized to terminate the running query. As a result,
             * it's preferable to provide the worksheet connection for reconnecting
             * to avoid this edge case.
             */
            if (this.isActiveQueryEditorWke && !this.isWkeConnLost) ids.push(this.activeWkeConn.id)
            return ids
        },
        showReconnDialog: {
            get() {
                return Boolean(this.lostConns.length)
            },
            set() {
                this.connIdsToBeReconnected.forEach(id =>
                    QueryConn.update({ where: id, data: { lost_cnn_err: {} } })
                )
            },
        },
    },
    methods: {
        ...mapActions({ fetchSrcSchemas: 'etlMem/fetchSrcSchemas' }),
        async deleteConns() {
            if (this.isActiveQueryEditorWke)
                await QueryConn.dispatch('cascadeDisconnectWkeConn', {
                    id: QueryConn.getters('getActiveWkeConn').id,
                })
            else
                await Promise.all(
                    this.connIdsToBeReconnected.map(id => QueryConn.dispatch('disconnect', { id }))
                )
        },
        async handleReconnect() {
            await QueryConn.dispatch('reconnectConns', {
                ids: this.connIdsToBeReconnected,
                onError: async () => await this.deleteConns(),
                onSuccess: async () => {
                    if (this.isActiveQueryEditorWke) await Worksheet.dispatch('handleInitialFetch')
                    else if (this.activeEtlTask.active_stage_index === this.ETL_STAGE_INDEX.SRC_OBJ)
                        await this.fetchSrcSchemas()
                },
            })
        },
    },
}
</script>

<style lang="scss">
.lost-conn-tbl {
    tbody {
        &:not(:last-of-type) {
            &::after,
            &:first-of-type::before {
                content: '';
                display: block;
                height: 12px;
            }
        }
    }
}
</style>
