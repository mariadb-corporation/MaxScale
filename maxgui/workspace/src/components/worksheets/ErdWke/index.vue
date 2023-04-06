<template>
    <mxs-split-pane
        v-model="sidebarPct"
        class="query-view__content"
        :boundary="ctrDim.width"
        :minPercent="minSidebarPct"
        :deactivatedMinPctZone="deactivatedMinSizeBarPoint"
        :maxPercent="maxSidebarPct"
        split="vert"
        progress
        revertRender
        @resizing="onResizing"
    >
        <template slot="pane-left">
            <er-sidebar-ctr />
        </template>
        <template slot="pane-right">
            <div class="fill-height">
                <mxs-erd :ctrDim="dim" :data="graphData" />
            </div>
        </template>
    </mxs-split-pane>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErSidebarCtr from '@wkeComps/ErdWke/ErSidebarCtr.vue'

export default {
    name: 'erd-wke',
    components: { ErSidebarCtr },
    props: {
        ctrDim: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            erd_sidebar_pct_width: state => state.prefAndStorage.erd_sidebar_pct_width,
            is_erd_sidebar_collapsed: state => state.prefAndStorage.is_erd_sidebar_collapsed,
        }),
        //TODO: Convert to get/set
        graphData() {
            return ErdTask.getters('getActiveGraphData') || {}
        },
        dim() {
            const { width, height } = this.ctrDim
            return { width: width - this.sidebarWidth, height }
        },
        sidebarWidth() {
            return this.$helpers.pctToPx({ pct: this.sidebarPct, containerPx: this.ctrDim.width })
        },
        minSidebarPct() {
            return this.$helpers.pxToPct({ px: 40, containerPx: this.ctrDim.width })
        },
        deactivatedMinSizeBarPoint() {
            return this.minSidebarPct * 3
        },
        maxSidebarPct() {
            return 100 - this.$helpers.pxToPct({ px: 40, containerPx: this.ctrDim.width })
        },
        defSidebarPct() {
            return this.$helpers.pxToPct({ px: 240, containerPx: this.ctrDim.width })
        },
        sidebarPct: {
            get() {
                if (this.is_erd_sidebar_collapsed) return this.minSidebarPct
                return this.erd_sidebar_pct_width
            },
            set(v) {
                this.SET_ERD_SIDEBAR_PCT_WIDTH(v)
            },
        },
    },
    watch: {
        is_erd_sidebar_collapsed(v) {
            if (!v && this.sidebarPct <= this.minSidebarPct) this.sidebarPct = this.defSidebarPct
        },
    },
    created() {
        this.handleSetDefSidebarPct()
    },
    methods: {
        ...mapMutations({
            SET_ERD_SIDEBAR_PCT_WIDTH: 'prefAndStorage/SET_ERD_SIDEBAR_PCT_WIDTH',
            SET_IS_ERD_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_ERD_SIDEBAR_COLLAPSED',
        }),
        handleSetDefSidebarPct() {
            if (!this.sidebarPct) this.sidebarPct = this.defSidebarPct
        },
        onResizing(v) {
            //auto collapse sidebar
            if (v <= this.minSidebarPct) this.SET_IS_ERD_SIDEBAR_COLLAPSED(true)
            else if (v >= this.deactivatedMinSizeBarPoint) this.SET_IS_ERD_SIDEBAR_COLLAPSED(false)
        },
    },
}
</script>
