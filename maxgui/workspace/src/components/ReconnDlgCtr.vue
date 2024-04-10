<template>
    <mxs-dlg
        v-model="showReconnDialog"
        :title="$mxs_t('errors.serverHasGoneAway')"
        minBodyWidth="624px"
        :onSave="handleReconnect"
        cancelText="disconnect"
        saveText="reconnect"
        :showCloseBtn="false"
        @after-cancel="deleteConns"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import EtlTask from '@wsModels/EtlTask'
import { ETL_STAGE_INDEX } from '@wsSrc/constants'

export default {
    name: 'reconn-dlg-ctr',
    computed: {
        activeWke() {
            return Worksheet.getters('activeRecord')
        },
        isActiveQueryEditorWke() {
            return Boolean(this.activeWke.query_editor_id)
        },
        isActiveEtlWke() {
            return Boolean(this.activeWke.etl_task_id)
        },
        activeEtlTask() {
            return EtlTask.getters('activeRecord')
        },
        activeQueryEditorConn() {
            return QueryConn.getters('activeQueryEditorConn')
        },
        activeConns() {
            if (this.isActiveQueryEditorWke)
                return [this.activeQueryEditorConn, QueryConn.getters('activeQueryTabConn')]
            if (this.isActiveEtlWke) return QueryConn.getters('activeEtlConns')
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
            return this.lostConnIds.includes(this.activeQueryEditorConn.id)
        },
        connIdsToBeReconnected() {
            let ids = this.lostConnIds
            /**
             * The QueryEditor connection is normally not included in 'lostConns'
             * since the `lost_cnn_err` is retrieved if the QueryEditor connection
             * is used for querying. The QueryEditor connection, on the other hand,
             * is solely utilized to terminate the running query. As a result,
             * it's preferable to provide the QueryEditor connection for reconnecting
             * to avoid this edge case.
             */
            if (this.isActiveQueryEditorWke && !this.isWkeConnLost)
                ids.push(this.activeQueryEditorConn.id)
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
        async deleteConns() {
            if (this.isActiveQueryEditorWke)
                await QueryConn.dispatch('cascadeDisconnect', {
                    id: this.activeQueryEditorConn.id,
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
                    if (this.isActiveQueryEditorWke)
                        await QueryEditor.dispatch('handleInitialFetch')
                    else if (this.activeEtlTask.active_stage_index === ETL_STAGE_INDEX.SRC_OBJ)
                        await EtlTask.dispatch('fetchSrcSchemas')
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
