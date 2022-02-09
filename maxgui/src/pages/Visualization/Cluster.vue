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
                :nodeSize="nodeSize"
                draggable
                :noDragNodes="noDragNodes"
                @on-node-dragStart="onNodeSwapStart"
                @on-node-move="onMove"
                @on-node-dragend="onNodeSwapEnd"
            >
                <template v-slot:rect-node-content="{ data: { node } }">
                    <cluster-node
                        :node="node"
                        :droppableTargets="droppableTargets"
                        :nodeTxtWrapperClassName="nodeTxtWrapperClassName"
                        @get-expanded-node="handleExpandedNode"
                        @cluster-node-height="
                            handleAssignNodeHeightMap({ height: $event, nodeId: node.id })
                        "
                    />
                </template>
            </tree-graph>
        </v-card>
        <confirm-dialog
            v-model="isConfDlgOpened"
            :title="confDlgTitle"
            :type="confDlgType"
            closeImmediate
            :onSave="onConfirm"
        >
            <template v-slot:body-prepend>
                <span v-html="confDlgBody" />
            </template>
        </confirm-dialog>
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
            srcNodeId: null,
            targetNodeId: null,
            isConfDlgOpened: false,
            confDlgTitle: '',
            confDlgBody: '',
            confDlgType: '',
            nodeTxtWrapperClassName: 'node-text-wrapper',
            expandedNodes: [],
            defClusterNodeHeight: 101,
            clusterNodeHeightMap: {},
            nodeGap: 24,
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
        noDragNodes() {
            // disable draggable on master node
            return [this.graphData.id] // root node of graphData is always a master server node
        },
        hasExpandedNode() {
            return Boolean(this.expandedNodes.length)
        },
        maxClusterNodeHeight() {
            const v = Math.max(...Object.values(this.clusterNodeHeightMap))
            if (this.$typy(v).isNumber) return v
            return this.defClusterNodeHeight
        },
        nodeSize() {
            if (this.hasExpandedNode) return [this.maxClusterNodeHeight + this.nodeGap, 320]
            return [this.defClusterNodeHeight + this.nodeGap, 320]
        },
    },
    async created() {
        this.$nextTick(() => this.setCtrDim())
        if (this.$typy(this.current_cluster).isEmptyObject) await this.fetchCluster()
    },
    methods: {
        ...mapActions({
            fetchClusterById: 'visualization/fetchClusterById',
            switchOver: 'monitor/switchOver',
        }),
        async fetchCluster() {
            await this.fetchClusterById(this.$route.params.id)
        },
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.graphContainer.$el
            this.ctrDim = { width: clientWidth, height: clientHeight - 2 }
        },
        handleExpandedNode(id) {
            if (this.expandedNodes.includes(id))
                this.expandedNodes.splice(this.expandedNodes.indexOf(id), 1)
            else this.expandedNodes.push(id)
        },
        handleAssignNodeHeightMap({ height, nodeId }) {
            this.$set(this.clusterNodeHeightMap, nodeId, height)
        },
        /**
         * This helps to store the current innerHTML of the dragging node to initialNodeInnerHTML
         */
        setDefNodeTxt() {
            let cloneEle = document.getElementsByClassName('rect-node-clone')
            if (cloneEle.length) {
                const nodeTxtWrapper = cloneEle[0].getElementsByClassName(
                    this.nodeTxtWrapperClassName
                )
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
         * This helps to change the text content(nodeTxtWrapperClassName) of the dragging node
         * @param {String} type - operation type
         */
        changeNodeTxt(type) {
            let cloneEle = document.getElementsByClassName('rect-node-clone')
            if (cloneEle.length) {
                let nodeTxtWrapper = cloneEle[0].getElementsByClassName(
                    this.nodeTxtWrapperClassName
                )
                switch (type) {
                    case 'switchover':
                        nodeTxtWrapper[0].innerHTML = this.$t('info.switchoverPromote')
                        break
                    default:
                        nodeTxtWrapper[0].innerHTML = this.initialNodeInnerHTML
                        break
                }
            }
        },
        /**
         *
         * @param {Object} srcNode - dragging node
         * @param {Object} targetNode - target node
         */
        detectOperationType({ srcNode, targetNode }) {
            if (srcNode.isMaster) this.opType = ''
            else if (targetNode.isMaster) {
                this.opType = 'switchover'
            }
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
            this.srcNodeId = e.dragged.getAttribute('node_id')
            const srcNode = this.graphDataHash[this.srcNodeId]

            const targetEle = e.related // drop target node element
            // listen on the target element
            targetEle.addEventListener('mouseleave', this.onNodeSwapLeave)

            this.targetNodeId = targetEle.getAttribute('node_id')
            const targetNode = this.graphDataHash[this.targetNodeId]
            this.isDroppable = this.droppableTargets.includes(this.targetNodeId)
            if (this.isDroppable) {
                this.detectOperationType({ srcNode, targetNode })
            } else this.onCancelSwap()
            // return false to cancel automatically swap by sortable.js
            cb(false)
        },
        onNodeSwapEnd() {
            if (this.isDroppable) {
                switch (this.opType) {
                    case 'switchover':
                        this.confDlgType = 'promote'
                        this.confDlgTitle = this.$t('switchover')
                        this.confDlgBody = this.$t('confirmations.switchoverPromote', {
                            newMaster: this.srcNodeId,
                        })
                        break
                }
                this.isConfDlgOpened = true
            }
            this.droppableTargets = []
        },
        async onConfirm() {
            switch (this.opType) {
                case 'switchover':
                    await this.switchOver({
                        monitorModule: this.current_cluster.module,
                        monitorId: this.current_cluster.id,
                        masterId: this.srcNodeId,
                        successCb: this.fetchCluster,
                    })
                    break
            }
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
