<template>
    <div class="fill-height d-flex flex-column">
        <er-toolbar-ctr v-model="graphConfigData" :height="toolbarHeight" />
        <mxs-erd
            :data="graphData"
            :ctrDim="diagramDim"
            :graphConfigData="graphConfigData"
            :isLaidOut="isLaidOut"
            @on-nodes-coords-update="onNodesCoordsUpdate"
            @dbl-click-node="onNodeDblClick"
        />
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
import ErdTask from '@wsModels/ErdTask'
import ErToolbarCtr from '@wkeComps/ErdWke/ErToolbarCtr.vue'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/config'

export default {
    name: 'diagram-ctr',
    components: { ErToolbarCtr },
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            graphConfigData: {
                link: {
                    color: '#424f62',
                    strokeWidth: 1,
                    isAttrToAttr: false,
                    opacity: 1,
                    [EVENT_TYPES.HOVER]: { color: 'white', invisibleOpacity: 1 },
                    [EVENT_TYPES.DRAGGING]: { color: 'white', invisibleOpacity: 1 },
                },
                marker: { width: 18 },
                linkShape: {
                    type: LINK_SHAPES.ORTHO,
                    entitySizeConfig: { rowHeight: 32, rowOffset: 4, headerHeight: 32 },
                },
            },
        }
    },
    computed: {
        activeErdTask() {
            return ErdTask.getters('getActiveErdTask')
        },
        erdTaskId() {
            return this.activeErdTask.id
        },
        graphData() {
            return this.$typy(this.activeErdTask, 'data').safeObjectOrEmpty
        },
        activeGraphConfig() {
            return this.$typy(this.activeErdTask, 'graph_config').safeObjectOrEmpty
        },
        isLaidOut() {
            return this.$typy(this.activeErdTask, 'is_laid_out').safeBoolean
        },
        toolbarHeight() {
            return 28
        },
        diagramDim() {
            return { width: this.dim.width, height: this.dim.height - this.toolbarHeight }
        },
    },
    watch: {
        graphConfigData: {
            deep: true,
            handler(v) {
                ErdTask.update({
                    where: this.erdTaskId,
                    data: {
                        graph_config: this.$helpers.immutableUpdate(this.activeGraphConfig, {
                            link: {
                                isAttrToAttr: { $set: v.link.isAttrToAttr },
                            },
                            linkShape: {
                                type: { $set: v.linkShape.type },
                            },
                        }),
                    },
                })
            },
        },
    },
    created() {
        this.graphConfigData = this.$helpers.lodash.merge(
            this.graphConfigData,
            this.activeGraphConfig
        )
    },
    methods: {
        onNodeDblClick({ node }) {
            ErdTask.update({
                where: this.erdTaskId,
                data: { graph_height_pct: 50, active_entity_id: node.id },
            })
        },
        onNodesCoordsUpdate(v) {
            ErdTask.update({
                where: this.erdTaskId,
                data: {
                    data: { ...this.graphData, nodes: v },
                    is_laid_out: true,
                },
            })
        },
    },
}
</script>
