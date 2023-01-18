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
                @click="add({ worksheet_id: activeWkeId })"
            >
                <v-icon size="18" color="deep-ocean">mdi-plus</v-icon>
            </v-btn>
        </div>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'

export default {
    name: 'query-tab-nav-toolbar-ctr',
    computed: {
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        activeQueryTabConn() {
            return QueryConn.getters('getActiveQueryTabConn')
        },
    },
    mounted() {
        this.$nextTick(() =>
            this.$emit('get-total-btn-width', this.$refs.buttonWrapper.clientWidth)
        )
    },
    methods: {
        add(param) {
            QueryTab.dispatch('handleAddQueryTab', param)
        },
    },
}
</script>
