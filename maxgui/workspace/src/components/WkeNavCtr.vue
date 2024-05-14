<template>
    <div class="d-flex flex-row">
        <v-tabs
            v-model="activeWkeID"
            show-arrows
            hide-slider
            :height="height"
            class="v-tabs--mxs-workspace-style query-tab-nav v-tabs--custom-small-pagination-btn flex-grow-0"
            :style="{ maxWidth: `calc(100% - ${pageToolbarBtnWidth + 1}px)` }"
            center-active
        >
            <v-tab
                v-for="wke in allWorksheets"
                :key="wke.id"
                :href="`#${wke.id}`"
                class="pa-0 tab-btn text-none"
                active-class="tab-btn--active"
            >
                <wke-nav-tab :wke="wke" />
            </v-tab>
        </v-tabs>
        <wke-toolbar @get-total-btn-width="pageToolbarBtnWidth = $event" />
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Worksheet from '@wsModels/Worksheet'
import WkeToolbar from '@wsComps/WkeToolbar.vue'
import WkeNavTab from '@wsComps/WkeNavTab.vue'

export default {
    name: 'wke-nav-ctr',
    components: { WkeToolbar, WkeNavTab },
    props: {
        height: { type: Number, required: true },
    },
    data() {
        return {
            pageToolbarBtnWidth: 128,
        }
    },
    computed: {
        activeWkeID: {
            get() {
                return Worksheet.getters('activeId')
            },
            set(v) {
                if (v) Worksheet.commit(state => (state.active_wke_id = v))
            },
        },
        allWorksheets() {
            return Worksheet.all()
        },
    },
}
</script>
