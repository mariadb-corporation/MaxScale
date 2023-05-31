<template>
    <div class="fill-height d-flex flex-column">
        <er-toolbar-ctr
            v-model="graphConfigData"
            :height="toolbarHeight"
            :zoom="panAndZoom.k"
            :isFitIntoView="isFitIntoView"
            @set-zoom="setZoom"
        />
        <mxs-erd
            ref="diagram"
            :panAndZoom.sync="panAndZoom"
            :data="graphData"
            :dim="diagramDim"
            :scaleExtent="scaleExtent"
            :graphConfigData="graphConfigData"
            :isLaidOut="isLaidOut"
            @on-rendered="fitIntoView"
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
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
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
            isFitIntoView: false,
            panAndZoom: { x: 0, y: 0, k: 1 },
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
            return 40
        },
        diagramDim() {
            return { width: this.dim.width, height: this.dim.height - this.toolbarHeight }
        },
        scaleExtent() {
            return [0.25, 2]
        },
        activeEntityId() {
            return ErdTask.getters('getActiveEntityId')
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
        panAndZoom: {
            deep: true,
            handler(v) {
                if (v.eventType && v.eventType == 'wheel') this.isFitIntoView = false
            },
        },
    },
    created() {
        this.graphConfigData = this.$helpers.lodash.merge(
            this.graphConfigData,
            this.activeGraphConfig
        )
    },
    activated() {
        this.watchActiveEntityId()
    },
    deactivated() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
    },
    beforeDestroy() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
    },
    methods: {
        watchActiveEntityId() {
            this.unwatch_activeEntityId = this.$watch('activeEntityId', () => {
                //TODO: Replace with method to zoom into the node
                this.fitIntoView()
            })
        },
        onNodeDblClick({ node }) {
            ErdTaskTmp.update({
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
        fitIntoView() {
            this.setZoom({ isFitIntoView: true })
        },
        calcFitZoom({ minX, maxX, minY, maxY }) {
            const graphWidth = maxX - minX
            const graphHeight = maxY - minY
            // scales with 2% padding
            const xScale = (this.diagramDim.width / graphWidth) * 0.98
            const yScale = (this.diagramDim.height / graphHeight) * 0.98
            // Choose the minimum scale among xScale, yScale, and the maximum allowed scale
            let k = Math.min(xScale, yScale, this.scaleExtent[1])
            // Clamp the scale value within the scaleExtent range
            k = Math.min(Math.max(k, this.scaleExtent[0]), this.scaleExtent[1])
            return k
        },
        /**
         * Auto adjust (zoom in or out) the contents of a graph
         * @param {Boolean} [param.isFitIntoView] - is fit into view
         * @param {Number} [param.v] - zoom value
         */
        setZoom({ isFitIntoView, v }) {
            this.isFitIntoView = isFitIntoView
            const graphExtent = this.$refs.diagram.getGraphExtent()
            const k = v ? v : this.calcFitZoom(graphExtent)
            const { minX, minY, maxX, maxY } = graphExtent
            const x = this.diagramDim.width / 2 - ((minX + maxX) / 2) * k
            const y = this.diagramDim.height / 2 - ((minY + maxY) / 2) * k
            this.panAndZoom = { x, y, k }
        },
    },
}
</script>
