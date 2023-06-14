<template>
    <div class="fill-height d-flex flex-column">
        <er-toolbar-ctr
            v-model="graphConfigData"
            :height="toolbarHeight"
            :zoom="panAndZoom.k"
            :isFitIntoView="isFitIntoView"
            @set-zoom="setZoom"
        />
        <entity-diagram
            v-if="diagramKey"
            ref="diagram"
            :key="diagramKey"
            :panAndZoom.sync="panAndZoom"
            :data="graphData"
            :dim="diagramDim"
            :scaleExtent="scaleExtent"
            :graphConfigData="graphConfigData"
            :isLaidOut="isLaidOut"
            :activeNodeId="activeEntityId"
            @on-rendered.once="fitIntoView"
            @on-nodes-coords-update="onNodesCoordsUpdate"
            @on-choose-node-opt="handleChooseNodeOpt"
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
/*
 * Emits:
 * - $emit('on-choose-node-opt', { type:string, node:object })
 */
import { mapMutations } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import QueryConn from '@wsModels/QueryConn'
import ErToolbarCtr from '@wkeComps/ErdWke/ErToolbarCtr.vue'
import EntityDiagram from '@wsSrc/components/worksheets/ErdWke/EntityDiagram.vue'
import { LINK_SHAPES } from '@wsSrc/components/worksheets/ErdWke/config'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'
import { min as d3Min, max as d3Max } from 'd3-array'

export default {
    name: 'diagram-ctr',
    components: { ErToolbarCtr, EntityDiagram },
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
            diagramKey: '',
        }
    },
    computed: {
        activeErdTask() {
            return ErdTask.getters('getActiveErdTask')
        },
        erdTaskId() {
            return this.activeErdTask.id
        },
        activeErdConn() {
            return QueryConn.getters('getActiveErdConn')
        },
        graphData() {
            return this.$typy(this.activeErdTask, 'data').safeObjectOrEmpty
        },
        graphNodes() {
            return this.$typy(this.graphData, 'nodes').safeArray
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
        erdTaskKey() {
            return this.$typy(ErdTask.getters('getActiveErdTaskTmp'), 'key').safeString
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
        this.watchErdTaskKey()
    },
    deactivated() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
        this.$typy(this.unwatch_erdTaskKey).safeFunction()
    },
    beforeDestroy() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
        this.$typy(this.unwatch_erdTaskKey).safeFunction()
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        watchActiveEntityId() {
            this.unwatch_activeEntityId = this.$watch('activeEntityId', v => {
                if (!v) this.fitIntoView()
            })
        },
        /**
         * If the users generate new ERD for existing ERD worksheet
         * or a blank ERD worksheet, erdTaskKey will be re-generated
         * so the diagram must be reinitialized
         */
        watchErdTaskKey() {
            this.unwatch_erdTaskKey = this.$watch(
                'erdTaskKey',
                v => {
                    if (v) this.diagramKey = v
                },
                { immediate: true }
            )
        },
        handleChooseNodeOpt(param) {
            if (this.activeErdConn.id) {
                this.$emit('on-choose-node-opt', param)
                // call in the next tick to ensure diagramDim height is up to date
                this.$nextTick(() => this.zoomIntoNode(param.node))
            } else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredConn')],
                    type: 'error',
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
        calcFitZoom({ extent: { minX, maxX, minY, maxY }, paddingPct = 2 }) {
            const graphWidth = maxX - minX
            const graphHeight = maxY - minY
            const xScale = (this.diagramDim.width / graphWidth) * (1 - paddingPct / 100)
            const yScale = (this.diagramDim.height / graphHeight) * (1 - paddingPct / 100)
            // Choose the minimum scale among xScale, yScale, and the maximum allowed scale
            let k = Math.min(xScale, yScale, this.scaleExtent[1])
            // Clamp the scale value within the scaleExtent range
            k = Math.min(Math.max(k, this.scaleExtent[0]), this.scaleExtent[1])
            return k
        },
        zoomIntoNode(node) {
            const minX = node.x - node.size.width / 2
            const minY = node.y - node.size.height / 2
            const maxX = minX + node.size.width
            const maxY = minY + node.size.height
            this.setZoom({
                isFitIntoView: true,
                customExtent: { minX, maxX, minY, maxY },
                /* add a padding of 20%, so there'd be some reserved space if the users
                 * alter the table by adding new column
                 */
                paddingPct: 20,
            })
        },
        /**
         * Get the correct dimension of the nodes for controlling the zoom
         */
        getGraphExtent() {
            return {
                minX: d3Min(this.graphNodes, n => n.x - n.size.width / 2),
                minY: d3Min(this.graphNodes, n => n.y - n.size.height / 2),
                maxX: d3Max(this.graphNodes, n => n.x + n.size.width / 2),
                maxY: d3Max(this.graphNodes, n => n.y + n.size.height / 2),
            }
        },
        /**
         * Auto adjust (zoom in or out) the contents of a graph
         * @param {Boolean} [param.isFitIntoView] - if it's true, v param will be ignored
         * @param {Object} [param.customExtent] - custom extent
         * @param {Number} [param.v] - zoom value
         */
        setZoom({ isFitIntoView = false, customExtent, v, paddingPct = 2 }) {
            this.isFitIntoView = isFitIntoView
            const extent = customExtent ? customExtent : this.getGraphExtent()
            const { minX, minY, maxX, maxY } = extent

            const k = isFitIntoView ? this.calcFitZoom({ extent, paddingPct }) : v
            const x = this.diagramDim.width / 2 - ((minX + maxX) / 2) * k
            const y = this.diagramDim.height / 2 - ((minY + maxY) / 2) * k

            this.panAndZoom = { x, y, k, transition: true }
        },
    },
}
</script>
