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
                    v-if="isWkeLoadingQueryResult"
                    class="ml-2"
                    size="16"
                    width="2"
                    color="primary"
                    indeterminate
                />
            </div>
            <v-btn v-show="isHovered" class="ml-1" icon x-small @click.stop.prevent="onDelete">
                <v-icon :size="8" :color="isConnBusy ? '' : 'error'">
                    $vuetify.icons.mxs_close
                </v-icon>
            </v-btn>
        </span>
    </v-hover>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'

export default {
    name: 'wke-nav-tab',
    props: {
        wke: { type: Object, required: true },
    },
    computed: {
        wkeId() {
            return this.wke.id
        },
        isWkeLoadingQueryResult() {
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
        isConnBusy() {
            return QueryConn.getters('getIsConnBusyByQueryTabId')(
                Worksheet.getters('getActiveQueryTabId')
            )
        },
    },
    methods: {
        async onDelete() {
            await Worksheet.dispatch('handleDeleteWke', this.wke.id)
        },
    },
}
</script>
