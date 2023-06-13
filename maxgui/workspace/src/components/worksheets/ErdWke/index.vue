<template>
    <div class="fill-height">
        <mxs-split-pane
            v-model="graphHeightPct"
            :boundary="dim.height"
            split="horiz"
            :minPercent="minErdPct"
            :maxPercent="maxErdPct"
            :deactivatedMaxPctZone="maxErdPct - (100 - maxErdPct) * 2"
            :disable="graphHeightPct === 100"
        >
            <template slot="pane-left">
                <diagram-ctr :dim="erdDim" @on-choose-node-opt="handleChooseNodeOpt" />
            </template>
            <template slot="pane-right">
                <entity-editor-ctr v-show="activeEntityId" :dim="editorDim" />
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
import { mapState } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'

export default {
    name: 'erd-wke',
    components: { DiagramCtr, EntityEditorCtr },
    props: {
        ctrDim: { type: Object, required: true },
    },
    computed: {
        ...mapState({ ENTITY_OPT_TYPES: state => state.mxsWorkspace.config.ENTITY_OPT_TYPES }),
        dim() {
            const { width, height } = this.ctrDim
            return { width: width, height: height }
        },
        erdDim() {
            return { width: this.ctrDim.width, height: this.erGraphHeight }
        },
        erGraphHeight() {
            return this.$helpers.pctToPx({
                pct: this.graphHeightPct,
                containerPx: this.dim.height,
            })
        },
        editorDim() {
            return { width: this.ctrDim.width, height: this.dim.height - this.erGraphHeight }
        },
        minErdPct() {
            return this.$helpers.pxToPct({
                px: this.activeEntityId ? 42 : 0,
                containerPx: this.dim.height,
            })
        },
        maxErdPct() {
            return 100 - this.minErdPct
        },
        activeEntityId() {
            return ErdTask.getters('getActiveEntityId')
        },
        activeErdTaskId() {
            return ErdTask.getters('getActiveErdTaskId')
        },
        graphHeightPct: {
            get() {
                return ErdTask.getters('getGraphHeightPct')
            },
            set(v) {
                ErdTaskTmp.update({
                    where: this.activeErdTaskId,
                    data: { graph_height_pct: v },
                })
            },
        },
    },
    methods: {
        handleChooseNodeOpt({ type, node }) {
            const { ALTER, DROP } = this.ENTITY_OPT_TYPES
            switch (type) {
                case ALTER: {
                    let data = { active_entity_id: node.id }
                    if (this.graphHeightPct === 100) data.graph_height_pct = 50
                    ErdTaskTmp.update({ where: this.activeErdTaskId, data })
                    break
                }
                case DROP:
                    //TODO: Handle DROP option
                    break
            }
        },
    },
}
</script>
