<template>
    <div class="d-flex flex-row">
        <v-tabs
            v-model="activeQueryTabId"
            show-arrows
            hide-slider
            :height="height"
            class="v-tabs--mxs-workspace-style query-tab-nav v-tabs--custom-small-pagination-btn flex-grow-0"
            :style="{ maxWidth: `calc(100% - ${queryTabNavToolbarWidth + 1}px)` }"
            center-active
        >
            <v-tab
                v-for="queryTab in queryTabsOfActiveWke"
                :key="`${queryTab.id}`"
                :href="`#${queryTab.id}`"
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active"
            >
                <query-tab-nav-item :queryTab="queryTab" />
            </v-tab>
        </v-tabs>
        <query-tab-nav-toolbar-ctr @get-total-btn-width="queryTabNavToolbarWidth = $event">
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </query-tab-nav-toolbar-ctr>
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabNavToolbarCtr from '@wkeComps/QueryEditor/QueryTabNavToolbarCtr.vue'
import QueryTabNavItem from '@wkeComps/QueryEditor/QueryTabNavItem.vue'
import saveFile from '@wsSrc/mixins/saveFile'

export default {
    name: 'query-tab-nav-ctr',
    components: { QueryTabNavToolbarCtr, QueryTabNavItem },
    mixins: [saveFile],
    props: { height: { type: Number, required: true } },
    data() {
        return {
            queryTabNavToolbarWidth: 0,
        }
    },
    computed: {
        activeQueryTabId: {
            get() {
                return QueryEditor.getters('getActiveQueryTabId')
            },
            set(v) {
                if (v)
                    QueryEditor.update({
                        where: QueryEditor.getters('getQueryEditorId'),
                        data: { active_query_tab_id: v },
                    })
            },
        },
        queryTabsOfActiveWke() {
            return QueryTab.getters('getActiveQueryTabs')
        },
    },
}
</script>
