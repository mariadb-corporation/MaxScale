<template>
    <page-wrapper fluid>
        <page-header @on-choose-op="onChooseOp" @on-count-done="fetchCluster" />
        <v-card
            ref="graphContainer"
            v-resize.quiet="setCtrDim"
            class="ml-6 mt-9 fill-height graph-card"
            outlined
        >
            <tree-graph
                v-if="ctrDim.height && !$typy(graphData).isEmptyObject"
                :data="graphData"
                :dim="ctrDim"
                :nodeSize="nodeSize"
                draggable
                :draggableGroup="{
                    name: 'tree-graph',
                    put: ['joinable-servers'], // allow nodes on joinable-servers to be dragged here
                }"
                :noDragNodes="noDragNodes"
                :expandedNodes="expandedNodes"
                :nodeDivHeightMap="clusterNodeHeightMap"
                :cloneClass="draggingStates.nodeCloneClass"
                @on-node-drag-start="onNodeDragStart"
                @on-node-dragging="onNodeDragging"
                @on-node-drag-end="onNodeDragEnd"
            >
                <template v-slot:rect-node-content="{ data: { node } }">
                    <!-- Render server-node only when `data` object is passed from tree-graph -->
                    <server-node
                        v-if="!$typy(node, 'data').isEmptyObject"
                        :key="`${uniqueKey}-${node.id}`"
                        :node="node"
                        :droppableTargets="draggingStates.droppableTargets"
                        :bodyWrapperClass="nodeTxtWrapperClassName"
                        :expandOnMount="expandOnMount"
                        @get-expanded-node="handleExpandedNode"
                        @node-height="
                            handleAssignNodeHeightMap({ height: $event, nodeId: node.id })
                        "
                        @on-choose-op="onChooseOp"
                    />
                </template>
            </tree-graph>
            <!-- TODO: Add joinable-servers component -->
        </v-card>
        <confirm-dialog
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :type="confDlg.type"
            :saveText="confDlgSaveTxt"
            :item="confDlg.targetNode"
            :smallInfo="confDlg.smallInfo"
            :closeImmediate="true"
            :onSave="onConfirm"
        >
            <template v-if="confDlg.type === SERVER_OP_TYPES.MAINTAIN" v-slot:body-append>
                <v-checkbox
                    v-model="confDlg.forceClosing"
                    class="small mt-2 mb-4"
                    :label="$t('forceClosing')"
                    color="primary"
                    hide-details
                />
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
import PageHeader from './PageHeader.vue'
import ServerNode from './ServerNode.vue'

export default {
    name: 'cluster',
    components: {
        'page-header': PageHeader,
        'server-node': ServerNode,
    },
    data() {
        return {
            // a key for triggering a re-render on server-node
            uniqueKey: this.$help.uuidv1(),
            ctrDim: {},
            // states for controlling drag behaviors
            defDraggingStates: {
                isDroppable: false,
                droppableTargets: [],
                initialNodeInnerHTML: null,
                draggingNodeId: null,
                droppingNodeId: null,
                nodeCloneClass: 'drag-node-clone',
            },
            draggingStates: {},
            //states for tree-graph
            expandedNodes: [],
            defClusterNodeHeight: 119,
            defClusterNodeWidth: 290,
            clusterNodeHeightMap: {},
            //states for server-node
            nodeTxtWrapperClassName: 'node-text-wrapper',
            // states for confirm-dialog
            defConfDlg: {
                opType: '',
                isOpened: false,
                title: '',
                type: '',
                targetNode: null,
                smallInfo: '',
                // states for server options
                opParams: '',
                forceClosing: false,
            },
            confDlg: {},
        }
    },
    computed: {
        ...mapState({
            current_cluster: state => state.visualization.current_cluster,
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
            SERVER_OP_TYPES: state => state.app_config.SERVER_OP_TYPES,
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
            return {
                width: this.defClusterNodeWidth,
                height: this.hasExpandedNode
                    ? this.maxClusterNodeHeight
                    : this.defClusterNodeHeight,
            }
        },
        expandOnMount() {
            if (
                this.$vuetify.breakpoint.height >= 1080 &&
                Object.keys(this.graphDataHash).length <= 4
            )
                return true
            return false
        },
        confDlgSaveTxt() {
            const { MAINTAIN } = this.SERVER_OP_TYPES
            const { RESET_REP, RELEASE_LOCKS, FAILOVER, SWITCHOVER } = this.MONITOR_OP_TYPES
            switch (this.confDlg.type) {
                case SWITCHOVER:
                    return 'promote'
                case RESET_REP:
                    return 'reset'
                case RELEASE_LOCKS:
                    return 'release'
                case FAILOVER:
                    return 'perform'
                case MAINTAIN:
                    return 'set'
                default:
                    return this.confDlg.type
            }
        },
    },
    async created() {
        this.resetDraggingStates()
        this.resetConfDlgStates()
        if (this.$typy(this.current_cluster).isEmptyObject) await this.fetchCluster()
    },
    mounted() {
        this.$nextTick(() => this.setCtrDim())
    },
    methods: {
        ...mapActions({
            fetchClusterById: 'visualization/fetchClusterById',
            manipulateMonitor: 'monitor/manipulateMonitor',
            setOrClearServerState: 'server/setOrClearServerState',
        }),
        async fetchCluster() {
            await this.fetchClusterById(this.$route.params.id)
        },
        setCtrDim() {
            const { clientHeight, clientWidth } = this.$refs.graphContainer.$el
            this.ctrDim = { width: clientWidth, height: clientHeight }
        },
        handleExpandedNode({ type, id }) {
            let target = this.expandedNodes.indexOf(id)
            switch (type) {
                case 'destroy':
                    this.$delete(this.expandedNodes, target)
                    break
                case 'update':
                    if (this.expandedNodes.includes(id)) this.expandedNodes.splice(target, 1)
                    else this.expandedNodes.push(id)
                    break
            }
        },
        handleAssignNodeHeightMap({ height, nodeId }) {
            this.$set(this.clusterNodeHeightMap, nodeId, height)
        },
        resetDraggingStates() {
            this.draggingStates = this.$help.lodash.cloneDeep(this.defDraggingStates)
        },
        resetConfDlgStates() {
            this.confDlg = this.$help.lodash.cloneDeep(this.defConfDlg)
        },
        /**
         * This helps to store the current innerHTML of the dragging node to initialNodeInnerHTML
         */
        setDefNodeTxt() {
            let cloneEle = document.getElementsByClassName(this.draggingStates.nodeCloneClass)

            if (cloneEle.length) {
                const nodeTxtWrapper = cloneEle[0].getElementsByClassName(
                    this.nodeTxtWrapperClassName
                )
                this.draggingStates.initialNodeInnerHTML = nodeTxtWrapper[0].innerHTML
            }
        },
        /**
         * This finds out which nodes in the cluster that the dragging node can be dropped to
         * @param {Object} node - dragging node to be dropped
         */
        detectDroppableTargets(node) {
            if (node.isMaster) {
                this.draggingStates.droppableTargets = []
            } else {
                //switchover: dragging a slave to a master
                this.draggingStates.droppableTargets = [node.masterServerName]
            }
        },
        /**
         * This helps to change the text content(nodeTxtWrapperClassName) of the dragging node
         * @param {String} type - operation type
         */
        changeNodeTxt(type) {
            let cloneEle = document.getElementsByClassName(this.draggingStates.nodeCloneClass)
            if (cloneEle.length) {
                let nodeTxtWrapper = cloneEle[0].getElementsByClassName(
                    this.nodeTxtWrapperClassName
                )
                const { SWITCHOVER } = this.MONITOR_OP_TYPES
                switch (type) {
                    case SWITCHOVER:
                        nodeTxtWrapper[0].innerHTML = this.$t(`monitorOps.info.${SWITCHOVER}`)
                        break
                    default:
                        nodeTxtWrapper[0].innerHTML = this.draggingStates.initialNodeInnerHTML
                        break
                }
            }
        },
        /**
         *
         * @param {Object} draggingNode - dragging node
         * @param {Object} droppingNode - dropping node
         */
        detectOperationType({ draggingNode, droppingNode }) {
            if (draggingNode.isMaster) this.confDlg.opType = ''
            else if (droppingNode.isMaster) {
                const { SWITCHOVER } = this.MONITOR_OP_TYPES
                this.confDlg.opType = SWITCHOVER
            }
            this.changeNodeTxt(this.confDlg.opType)
        },
        /**
         * This helps to change the dragging node's innerHTML back
         * to its initial value. i.e. `initialNodeInnerHTML`
         */
        onDraggingMouseLeave() {
            this.changeNodeTxt()
            this.onCancelDrag()
        },
        onCancelDrag() {
            this.draggingStates.isDroppable = false
        },
        onNodeDragStart(e) {
            document.body.classList.add('cursor--all-move')
            let nodeId = e.item.getAttribute('node_id')
            const node = this.graphDataHash[nodeId]
            this.setDefNodeTxt()
            this.detectDroppableTargets(node)
        },
        onNodeDragging(e, cb) {
            this.draggingStates.draggingNodeId = e.dragged.getAttribute('node_id')
            const draggingNode = this.graphDataHash[this.draggingStates.draggingNodeId]

            const dropEle = e.related // drop target node element
            // listen on the target element
            dropEle.addEventListener('mouseleave', this.onDraggingMouseLeave)
            const droppingNodeId = dropEle.getAttribute('node_id')
            const droppingNode = this.graphDataHash[droppingNodeId]
            const isDroppable = this.draggingStates.droppableTargets.includes(droppingNodeId)
            this.draggingStates = {
                ...this.draggingStates,
                droppingNodeId,
                isDroppable,
            }
            if (isDroppable) {
                this.detectOperationType({ draggingNode, droppingNode })
            } else this.onCancelDrag()
            // return false to cancel automatically swap by sortable.js
            cb(false)
        },
        onNodeDragEnd() {
            if (this.draggingStates.isDroppable) {
                const { SWITCHOVER } = this.MONITOR_OP_TYPES
                switch (this.confDlg.opType) {
                    case SWITCHOVER:
                        this.confDlg.type = this.confDlg.opType
                        this.confDlg.title = this.$t(`monitorOps.actions.${this.confDlg.opType}`)
                        this.confDlg.targetNode = { id: this.draggingStates.draggingNodeId }
                        break
                }
                this.confDlg.isOpened = true
            }
            this.draggingStates.droppableTargets = []
            document.body.classList.remove('cursor--all-move')
        },
        /**
         * A node can be expanded or collapsed by the user's interaction, so the
         * height of each node is dynamic.
         * This method should be called when the order of the nodes has changed.
         * e.g. after a switchover or a rejoin action.
         * This helps to get the accurate height of each node because
         * the `clusterNodeHeightMap` state won't be updated if the `server-node`
         * is not re-rendered.
         */
        triggerRerenderNodes() {
            this.uniqueKey = this.$help.uuidv1()
        },
        //Reset states after confirming an action
        async handleResetStates() {
            const { SWITCHOVER } = this.MONITOR_OP_TYPES
            switch (this.confDlg.opType) {
                case SWITCHOVER:
                    this.triggerRerenderNodes()
                    this.resetDraggingStates()
                    break
            }
            this.resetConfDlgStates()
        },
        async handleCallAsyncCmd() {
            const { SWITCHOVER } = this.MONITOR_OP_TYPES
            let payload = {
                type: this.confDlg.opType,
                callback: this.fetchCluster,
                id: this.current_cluster.id,
                opParams: {
                    moduleType: this.current_cluster.module,
                    params: '',
                },
            }
            switch (this.confDlg.opType) {
                case SWITCHOVER:
                    payload.opParams.params = `&${this.draggingStates.draggingNodeId}`
                    break
                default:
            }
            await this.manipulateMonitor(payload)
        },
        async onConfirm() {
            const {
                SWITCHOVER,
                STOP,
                START,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
            } = this.MONITOR_OP_TYPES
            const { MAINTAIN, CLEAR, DRAIN } = this.SERVER_OP_TYPES
            let payload = {
                type: this.confDlg.opType,
                callback: this.fetchCluster,
            }
            switch (this.confDlg.opType) {
                case SWITCHOVER:
                case RESET_REP:
                case RELEASE_LOCKS:
                case FAILOVER:
                    await this.handleCallAsyncCmd()
                    break
                case STOP:
                case START:
                    await this.manipulateMonitor({
                        ...payload,
                        id: this.current_cluster.id,
                        opParams: this.confDlg.opParams,
                    })
                    break
                case DRAIN:
                case CLEAR:
                case MAINTAIN:
                    await this.setOrClearServerState({
                        ...payload,
                        id: this.confDlg.targetNode.id,
                        opParams: this.confDlg.opParams,
                        forceClosing: this.confDlg.forceClosing,
                    })
                    break
            }
            this.handleResetStates()
        },
        onChooseOp({ op: { type, text, info, params }, target: { id } }) {
            this.confDlg = {
                ...this.confDlg,
                type,
                opType: type,
                title: text,
                opParams: params,
                targetNode: { id },
                smallInfo: info,
                isOpened: true,
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
