<template>
    <div class="d-flex flex-row">
        <v-tabs
            v-model="activeId"
            show-arrows
            hide-slider
            :height="height"
            class="v-tabs--mxs-workspace-style query-tab-nav v-tabs--custom-small-pagination-btn flex-grow-0"
            :style="{ maxWidth: `calc(100% - ${queryTabNavToolbarWidth + 1}px)` }"
            center-active
        >
            <v-tab
                v-for="tab in queryTabs"
                :key="`${tab.id}`"
                :href="`#${tab.id}`"
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active"
            >
                <query-tab-nav-item :queryTab="tab" @delete="handleDeleteTab" />
            </v-tab>
        </v-tabs>
        <query-tab-nav-toolbar
            :activeQueryTabConn="activeQueryTabConn"
            @add="addTab"
            @edit-conn="openCnnDlg"
            @get-total-btn-width="queryTabNavToolbarWidth = $event"
        >
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </query-tab-nav-toolbar>
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations } from 'vuex'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabNavToolbar from '@wkeComps/QueryEditor/QueryTabNavToolbar.vue'
import QueryTabNavItem from '@wkeComps/QueryEditor/QueryTabNavItem.vue'
import { QUERY_CONN_BINDING_TYPES } from '@wsSrc/constants'

export default {
    name: 'query-tab-nav-ctr',
    components: { QueryTabNavToolbar, QueryTabNavItem },
    props: {
        queryEditorId: { type: String, required: true },
        activeQueryTabId: { type: String, required: true },
        activeQueryTabConn: { type: Object, required: true },
        queryTabs: { type: Array, required: true },
        height: { type: Number, required: true },
    },
    data() {
        return {
            queryTabNavToolbarWidth: 0,
        }
    },
    computed: {
        activeId: {
            get() {
                return this.activeQueryTabId
            },
            set(v) {
                if (v)
                    QueryEditor.update({
                        where: this.queryEditorId,
                        data: { active_query_tab_id: v },
                    })
            },
        },
    },
    methods: {
        ...mapMutations({ SET_CONN_DLG: 'mxsWorkspace/SET_CONN_DLG' }),
        async handleDeleteTab(id) {
            if (this.queryTabs.length === 1) QueryTab.dispatch('refreshLastQueryTab', id)
            else await QueryTab.dispatch('handleDeleteQueryTab', id)
        },
        addTab() {
            QueryTab.dispatch('handleAddQueryTab', {
                query_editor_id: this.queryEditorId,
                schema: this.activeQueryTabConn.active_db,
            })
        },
        openCnnDlg() {
            this.SET_CONN_DLG({ is_opened: true, type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR })
        },
    },
}
</script>
