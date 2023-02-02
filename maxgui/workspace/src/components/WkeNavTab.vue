<template>
    <v-hover v-slot="{ hover: isHovered }">
        <span
            :style="{ width: '160px' }"
            class="fill-height d-flex align-center justify-space-between px-3 tab-name"
        >
            <div class="d-inline-flex align-center">
                <mxs-truncate-str
                    autoID
                    :tooltipItem="{ txt: wke.name, nudgeLeft: 20 }"
                    :maxWidth="110"
                />
                <v-progress-circular
                    v-if="isRunning"
                    class="ml-2"
                    size="16"
                    width="2"
                    color="primary"
                    indeterminate
                />
            </div>
            <v-btn
                v-if="!isRunning"
                v-show="isHovered"
                class="del-tab-btn ml-1"
                icon
                x-small
                @click.stop.prevent="onDelete"
            >
                <v-icon :size="8" color="error">
                    $vuetify.icons.mxs_close
                </v-icon>
            </v-btn>
        </span>
    </v-hover>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Worksheet from '@wsModels/Worksheet'
import QueryTab from '@wsModels/QueryTab'
import EtlTask from '@wsModels/EtlTask'
import QueryResult from '@wsModels/QueryResult'
import { mapState } from 'vuex'

export default {
    name: 'wke-nav-tab',
    props: {
        wke: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
        }),
        wkeId() {
            return this.wke.id
        },
        isOneOfQueryTabRunning() {
            const queryTabs = QueryTab.getters('getQueryTabsByWkeId')(this.wkeId)
            let isLoading = false
            for (const { id } of queryTabs) {
                if (QueryResult.getters('getLoadingQueryResultByQueryTabId')(id)) {
                    isLoading = true
                    break
                }
            }
            return isLoading
        },
        isRunningETL() {
            const etlTask = EtlTask.find(this.$typy(this.wke, 'active_etl_task_id').safeString)
            return this.$typy(etlTask, 'status').safeString === this.ETL_STATUS.RUNNING
        },
        isRunning() {
            return this.isOneOfQueryTabRunning || this.isRunningETL
        },
    },
    methods: {
        async onDelete() {
            await Worksheet.dispatch('handleDeleteWke', this.wke.id)
        },
    },
}
</script>
