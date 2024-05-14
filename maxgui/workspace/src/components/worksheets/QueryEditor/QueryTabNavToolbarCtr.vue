<template>
    <div
        class="query-tab-nav-toolbar-ctr d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border"
    >
        <div ref="buttonWrapper" class="d-flex align-center px-2">
            <v-btn
                :disabled="$typy(activeQueryTabConn).isEmptyObject"
                small
                class="float-left add-query-tab-btn"
                icon
                @click="add({ query_editor_id: queryEditorId })"
            >
                <v-icon size="18" color="blue-azure">mdi-plus</v-icon>
            </v-btn>
        </div>
        <div ref="toolbarRight" class="ml-auto d-flex align-center mx-3 fill-height">
            <mxs-tooltip-btn
                :tooltipProps="{ disabled: !connectedServerName }"
                x-small
                text
                color="primary"
                @click="SET_IS_CONN_DLG_OPENED(true)"
            >
                <template v-slot:btn-content>
                    <v-icon size="14" color="primary" class="mr-1">
                        mdi-server
                    </v-icon>
                    {{ connectedServerName ? connectedServerName : $mxs_t('connect') }}
                </template>
                {{ $mxs_t('changeConn') }}
            </mxs-tooltip-btn>
            <!-- A slot for SkySQL Query Editor in service details page where the worksheet tab is hidden  -->
            <slot name="query-tab-nav-toolbar-right" />
        </div>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryConn from '@wsModels/QueryConn'
import { mapMutations } from 'vuex'

export default {
    name: 'query-tab-nav-toolbar-ctr',
    components: {},
    computed: {
        queryEditorId() {
            return QueryEditor.getters('getQueryEditorId')
        },
        activeQueryTabConn() {
            return QueryConn.getters('getActiveQueryTabConn')
        },
        activeQueryEditorConn() {
            return QueryConn.getters('getQueryEditorConn')
        },
        connectedServerName() {
            return this.$typy(this.activeQueryEditorConn, 'meta.name').safeString
        },
    },
    watch: {
        connectedServerName() {
            this.calcWidth()
        },
    },
    mounted() {
        this.calcWidth()
    },
    methods: {
        ...mapMutations({
            SET_IS_CONN_DLG_OPENED: 'mxsWorkspace/SET_IS_CONN_DLG_OPENED',
        }),
        calcWidth() {
            this.$nextTick(() =>
                this.$emit(
                    'get-total-btn-width',
                    // (24 padding mx-3)
                    this.$refs.buttonWrapper.clientWidth + this.$refs.toolbarRight.clientWidth + 24
                )
            )
        },
        add(param) {
            QueryTab.dispatch('handleAddQueryTab', param)
        },
    },
}
</script>
