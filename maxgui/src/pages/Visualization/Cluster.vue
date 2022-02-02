<template>
    <page-wrapper>
        <cluster-page-header />
        <v-card
            ref="graphContainer"
            v-resize.quiet="setCtrDim"
            class="ml-6 mt-9 fill-height graph-card"
            outlined
        >
            <tree-graph
                v-if="ctrDim.height"
                :style="{ width: `${ctrDim.width}px`, height: `${ctrDim.height}px` }"
                :data="graphData"
                :dim="ctrDim"
                :nodeSize="[125, 320]"
                draggable
                @on-node-dragStart="onNodeSwapStart"
                @on-node-move="onMove"
                @on-node-dragend="onNodeSwapEnd"
            >
                <template v-slot:rect-node-content="{ data: { node } }">
                    <cluster-node :node="node" :droppableTargets="droppableTargets" />
                </template>
            </tree-graph>
        </v-card>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import TreeGraph from './TreeGraph.vue'
import ClusterPageHeader from './ClusterPageHeader.vue'
import ClusterNode from './ClusterNode.vue'

export default {
    name: 'cluster',
    components: {
        'tree-graph': TreeGraph,
        'cluster-page-header': ClusterPageHeader,
        'cluster-node': ClusterNode,
    },
    data() {
        return {
            ctrDim: {},
            isDroppable: false,
            droppableTargets: [],
            opType: '',
            initialNodeInnerHTML: null,
        }
    },
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
        }),
        graphData() {
            return this.$typy(this.current_cluster, 'children[0]').safeObjectOrEmpty
        },
        graphDataHash() {
            let hash = {}
            const getAllItemsPerChildren = item => {
                hash[item.id] = item
                if (item.children) return item.children.map(n => getAllItemsPerChildren(n))
            }
            getAllItemsPerChildren(this.graphData)
            return hash
        },
    },
    async created() {
        this.$nextTick(() => this.setCtrDim())
        if (this.$typy(this.current_cluster).isEmptyObject)
            await this.fetchClusterById(this.$route.params.id)
    },
    methods: {
        ...mapActions({
            fetchClusterById: 'visualization/fetchClusterById',
        }),
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.graphContainer.$el
            this.ctrDim = { width: clientWidth, height: clientHeight - 2 }
        },
        /**
         * This helps to store the current innerHTML of the dragging node to initialNodeInnerHTML
         */
        setDefNodeTxt() {
            let cloneEle = document.getElementsByClassName('rect-node-clone')
            if (cloneEle.length) {
                const nodeTxtWrapper = cloneEle[0].getElementsByClassName('node-text-wrapper')
                this.initialNodeInnerHTML = nodeTxtWrapper[0].innerHTML
            }
        },
        /**
         * This finds out which nodes in the cluster that the dragging node can be dropped to
         * @param {Object} node - dragging node to be dropped
         */
        detectDroppableTargets(node) {
            if (node.isMaster) {
                this.droppableTargets = []
            } else {
                //switchover: dragging a slave to a master
                this.droppableTargets = [node.masterServerName]
            }
        },
        /**
         * This helps to change the text content(node-text-wrapper) of the dragging node
         * @param {String} type - operation type
         */
        changeNodeTxt(type) {
            let cloneEle = document.getElementsByClassName('rect-node-clone')
            if (cloneEle.length) {
                let nodeTxtWrapper = cloneEle[0].getElementsByClassName('node-text-wrapper')
                switch (type) {
                    case 'switchover': {
                        const newInnerHTML = `Promote as Master (Switchover)`
                        nodeTxtWrapper[0].innerHTML = newInnerHTML
                        break
                    }
                    default:
                        nodeTxtWrapper[0].innerHTML = this.initialNodeInnerHTML
                        break
                }
            }
        },
        /**
         *
         * @param {Object} draggingNode - dragging node
         */
        detectOperationType(draggingNode) {
            if (draggingNode.isMaster) this.opType = ''
            else this.opType = 'switchover'
            this.changeNodeTxt(this.opType)
        },
        onNodeSwapStart(e) {
            let nodeId = e.item.getAttribute('node_id')
            const node = this.graphDataHash[nodeId]
            this.setDefNodeTxt()
            this.detectDroppableTargets(node)
        },
        /**
         * This helps to change the dragging node's innerHTML back
         * to its initial value. i.e. `initialNodeInnerHTML`
         */
        onNodeSwapLeave() {
            this.changeNodeTxt()
            this.onCancelSwap()
        },
        onCancelSwap() {
            this.isDroppable = false
        },
        onMove(e, cb) {
            const draggingNode = this.graphDataHash[e.dragged.getAttribute('node_id')]
            const targetEle = e.related // drop target node element
            // listen on the target element
            targetEle.addEventListener('mouseleave', this.onNodeSwapLeave)
            const dropTarget = targetEle.getAttribute('node_id')
            this.isDroppable = this.droppableTargets.includes(dropTarget)
            if (this.isDroppable) {
                this.detectOperationType(draggingNode)
            } else this.onCancelSwap()
            // return false to cancel automatically swap by sortable.js
            cb(false)
        },
        onNodeSwapEnd() {
            if (this.isDroppable) {
                switch (this.opType) {
                    case 'switchover':
                        // TODO: open switchover confirm dialog
                        break
                }
            }
            this.droppableTargets = []
        },
    },
}
</script>

<style lang="scss" scoped>
.graph-card {
    border: 1px solid #e3e6ea !important;
    box-sizing: content-box !important;
}
</style>
