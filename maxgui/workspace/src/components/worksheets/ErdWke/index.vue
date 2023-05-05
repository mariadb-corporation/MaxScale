<template>
    <div class="fill-height">
        <mxs-split-pane
            v-model="erdPctHeight"
            :boundary="dim.height"
            split="horiz"
            :minPercent="minErdPct"
            :maxPercent="maxErdPct"
            :deactivatedMaxPctZone="maxErdPct - (100 - maxErdPct) * 2"
        >
            <template slot="pane-left">
                <diagram-ctr :dim="erdDim" />
            </template>
            <template slot="pane-right">
                <entity-editor-ctr />
            </template>
        </mxs-split-pane>
    </div>
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
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'

export default {
    name: 'erd-wke',
    components: { DiagramCtr, EntityEditorCtr },
    props: {
        ctrDim: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            erd_pct_height: state => state.prefAndStorage.erd_pct_height,
        }),
        dim() {
            const { width, height } = this.ctrDim
            return { width: width, height: height }
        },
        erdDim() {
            return { width: this.ctrDim.width, height: this.erGraphHeight }
        },
        erGraphHeight() {
            return this.$helpers.pctToPx({
                pct: this.erdPctHeight,
                containerPx: this.dim.height,
            })
        },
        erdPctHeight: {
            get() {
                return this.erd_pct_height
            },
            set(v) {
                this.SET_ERD_PCT_HEIGHT(v)
            },
        },
        minErdPct() {
            //TODO: If there is no entity chosen in the diagram, return 0
            return this.$helpers.pxToPct({ px: 26, containerPx: this.dim.height })
        },
        maxErdPct() {
            return 100 - this.minErdPct
        },
    },
    methods: {
        ...mapMutations({
            SET_ERD_PCT_HEIGHT: 'prefAndStorage/SET_ERD_PCT_HEIGHT',
        }),
    },
}
</script>
