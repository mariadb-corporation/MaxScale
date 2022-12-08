<template>
    <v-tooltip
        :disabled="!$typy(wkeConn, 'name').safeString"
        top
        transition="slide-x-transition"
        content-class="shadow-drop white"
    >
        <template v-slot:activator="{ on }">
            <div
                style="min-width:160px"
                class="fill-height d-flex align-center justify-space-between px-3"
                v-on="on"
            >
                <div class="d-inline-flex align-center">
                    <span class="tab-name d-inline-block text-truncate" style="max-width:88px">
                        {{ worksheet.name }}
                    </span>
                    <v-progress-circular
                        v-if="isWkeLoadingQueryResult"
                        class="ml-2"
                        size="16"
                        width="2"
                        color="primary"
                        indeterminate
                    />
                </div>
                <v-btn
                    v-if="totalWorksheets > 1"
                    class="ml-1 del-tab-btn"
                    icon
                    x-small
                    :disabled="isConnBusy"
                    @click.stop.prevent="deleteWke"
                >
                    <v-icon size="8" :color="isConnBusy ? '' : 'error'">
                        $vuetify.icons.mxs_close
                    </v-icon>
                </v-btn>
            </div>
        </template>
        <span class="mxs-color-helper text-text py-2 px-4">
            {{ $mxs_t('connectedTo') }}
            {{ $typy(wkeConn, 'name').safeString }}
        </span>
    </v-tooltip>
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
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import QueryResult from '@queryEditorSrc/store/orm/models/QueryResult'

export default {
    name: 'wke-nav-tab',
    props: {
        worksheet: { type: Object, required: true },
    },
    computed: {
        totalWorksheets() {
            return Worksheet.all().length
        },
        wkeId() {
            return this.worksheet.id
        },
        wkeConn() {
            return QueryConn.getters('getWkeConnByWkeId')(this.wkeId)
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
        deleteWke() {
            Worksheet.dispatch('handleDeleteWke', this.wkeId)
        },
    },
}
</script>
