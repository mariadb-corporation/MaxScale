<template>
    <page-wrapper fluid class="fill-height">
        <monitor-page-header
            :targetMonitor="{
                id: current_cluster.id,
                attributes: $typy(current_cluster, 'monitorData').safeObjectOrEmpty,
            }"
            :successCb="monitorOpCallback"
            shouldFetchCsStatus
            @chosen-op-type="monitorOpType = $event"
            @on-count-done="fetchCluster"
        >
            <template v-slot:page-title="{ pageId }">
                <router-link :to="`/dashboard/monitors/${pageId}`" class="rsrc-link">
                    {{ pageId }}
                </router-link>
            </template>
        </monitor-page-header>
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
                :draggable="isAdmin"
                :draggableGroup="{
                    name: 'tree-graph',
                    put: ['joinable-servers'], // allow nodes on joinable-servers to be dragged here
                }"
                :noDragNodes="noDragNodes"
                :expandedNodes="expandedNodes"
                :nodeDivHeightMap="clusterNodeHeightMap"
                :cloneClass="draggingStates.nodeCloneClass"
                @on-node-drag-start="onNodeDragStart({ e: $event, from: 'tree' })"
                @on-node-dragging="onNodeDragging({ e: $event, from: 'tree' })"
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
            <joinable-servers
                v-if="joinableServerNodes.length"
                :data="joinableServerNodes"
                :draggableGroup="{ name: 'joinable-servers' }"
                :cloneClass="draggingStates.nodeCloneClass"
                :bodyWrapperClass="nodeTxtWrapperClassName"
                :droppableTargets="draggingStates.droppableTargets"
                :dim="ctrDim"
                :draggable="isAdmin"
                @on-choose-op="onChooseOp"
                @on-drag-start="onNodeDragStart({ e: $event, from: 'standaloneNode' })"
                @on-dragging="onNodeDragging({ e: $event, from: 'standaloneNode' })"
                @on-drag-end="onNodeDragEnd"
            />
        </v-card>
        <!-- Dialog for drag/drop actions and server operations -->
        <confirm-dlg
            v-model="confDlg.isOpened"
            :title="confDlg.title"
            :saveText="confDlgSaveTxt"
            :type="confDlg.type"
            :item="confDlg.targetNode"
            :smallInfo="confDlg.smallInfo"
            :closeImmediate="true"
            :onSave="onConfirm"
        >
            <template v-if="confDlg.type === SERVER_OP_TYPES.MAINTAIN" v-slot:body-append>
                <v-checkbox
                    v-model="confDlg.forceClosing"
                    class="v-checkbox--mariadb mt-2 mb-4"
                    :label="$mxs_t('forceClosing')"
                    color="primary"
                    dense
                    hide-details
                />
            </template>
        </confirm-dlg>
    </page-wrapper>
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
import { mapState, mapActions, mapGetters } from 'vuex'
import ServerNode from './ServerNode.vue'
import JoinableServers from './JoinableServers'
import { SERVER_OP_TYPES, MONITOR_OP_TYPES } from '@src/constants'

export default {
    name: 'cluster',
    components: {
        'server-node': ServerNode,
        'joinable-servers': JoinableServers,
    },
    data() {
        return {
            // a key for triggering a re-render on server-node
            uniqueKey: this.$helpers.uuidv1(),
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
            // states for confirm-dlg
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
            monitorOpType: '',
        }
    },
    computed: {
        ...mapGetters({ genNode: 'visualization/genNode', isAdmin: 'user/isAdmin' }),
        ...mapState({ current_cluster: state => state.visualization.current_cluster }),
        graphData() {
            return this.$typy(this.current_cluster, 'children[0]').safeObjectOrEmpty
        },
        treeHash() {
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
            if (this.$vuetify.breakpoint.height >= 1080 && Object.keys(this.treeHash).length <= 4)
                return true
            return false
        },
        confDlgSaveTxt() {
            const { MAINTAIN } = this.SERVER_OP_TYPES
            const { REJOIN, SWITCHOVER } = this.MONITOR_OP_TYPES
            switch (this.confDlg.type) {
                case SWITCHOVER:
                    return 'promote'
                case REJOIN:
                    return 'rejoin'
                case MAINTAIN:
                    return 'set'
                default:
                    return this.confDlg.type
            }
        },
        masterNode() {
            return this.$typy(this.current_cluster, 'children[0]').safeObjectOrEmpty
        },
        serverInfo() {
            return this.$typy(this.current_cluster, 'monitorData.monitor_diagnostics.server_info')
                .safeArray
        },
        masterNodeChildren() {
            return this.$helpers.flattenTree(this.$typy(this.masterNode, 'children').safeArray)
        },
        joinableServerNodes() {
            const joinableServers = this.serverInfo.filter(
                s =>
                    s.name !== this.masterNode.name &&
                    this.masterNodeChildren.every(n => n.name !== s.name)
            )
            return joinableServers.map(server => ({
                id: server.name,
                data: this.genNode({ server }),
            }))
        },
        standaloneNodeHash() {
            return this.$helpers.lodash.keyBy(this.joinableServerNodes, 'id')
        },
    },
    async created() {
        this.SERVER_OP_TYPES = SERVER_OP_TYPES
        this.MONITOR_OP_TYPES = MONITOR_OP_TYPES
        this.resetDraggingStates()
        this.resetConfDlgStates()
        await this.fetchCluster()
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
            this.draggingStates = this.$helpers.lodash.cloneDeep(this.defDraggingStates)
        },
        resetConfDlgStates() {
            this.confDlg = this.$helpers.lodash.cloneDeep(this.defConfDlg)
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
                //switchover or rejoin: dragging a slave to a master
                this.draggingStates.droppableTargets = [this.masterNode.name]
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
                const { SWITCHOVER, REJOIN } = this.MONITOR_OP_TYPES
                switch (type) {
                    case SWITCHOVER:
                    case REJOIN:
                        nodeTxtWrapper[0].innerHTML = this.$mxs_t(`monitorOps.info.${type}`)
                        break
                    default:
                        nodeTxtWrapper[0].innerHTML = this.draggingStates.initialNodeInnerHTML
                        break
                }
            }
        },
        /**
         *
         * @param {Object} param.draggingNode - dragging node
         * @param {Object} param.droppingNode - dropping node
         * @param {String} param.from - from either tree-graph (tree) or joinable-servers (standaloneNode)
         */
        detectOperationType({ draggingNode, droppingNode, from }) {
            if (draggingNode.isMaster) this.confDlg.opType = ''
            else if (droppingNode.isMaster) {
                const { SWITCHOVER, REJOIN } = this.MONITOR_OP_TYPES
                switch (from) {
                    case 'tree':
                        this.confDlg.opType = SWITCHOVER
                        break
                    case 'standaloneNode':
                        this.confDlg.opType = REJOIN
                }
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
        /**
         * @param {Object} param.e - drag start event
         * @param {String} param.from - from either tree-graph (tree) or joinable-servers (standaloneNode)
         */
        onNodeDragStart({ e, from }) {
            document.body.classList.add('cursor--all-move')
            const nodeId = e.item.getAttribute('node_id'),
                node = this[`${from}Hash`][nodeId]
            this.setDefNodeTxt()
            this.detectDroppableTargets(node)
        },
        /**
         * @param {Object} param.e - dragging event
         * @param {String} param.from - from either tree-graph (tree) or joinable-servers (standaloneNode)
         */
        onNodeDragging({ e, from }) {
            const draggingNodeId = e.dragged.getAttribute('node_id')
            this.draggingStates.draggingNodeId = draggingNodeId
            const draggingNode = this[`${from}Hash`][draggingNodeId]
            const dropEle = e.related // drop target node element
            const droppingNodeId = dropEle.getAttribute('node_id')
            const isDroppable = this.draggingStates.droppableTargets.includes(droppingNodeId)

            if (isDroppable) {
                // listen on the target element
                dropEle.addEventListener('mouseleave', this.onDraggingMouseLeave)
                const droppingNode = this.treeHash[droppingNodeId]
                this.detectOperationType({ draggingNode, droppingNode, from })
            } else this.onCancelDrag()

            this.draggingStates = {
                ...this.draggingStates,
                droppingNodeId,
                isDroppable,
            }
        },
        onNodeDragEnd() {
            if (this.draggingStates.isDroppable) {
                const { SWITCHOVER, REJOIN } = this.MONITOR_OP_TYPES
                switch (this.confDlg.opType) {
                    case SWITCHOVER:
                    case REJOIN:
                        this.confDlg.type = this.confDlg.opType
                        this.confDlg.title = this.$mxs_t(
                            `monitorOps.actions.${this.confDlg.opType}`
                        )
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
            this.uniqueKey = this.$helpers.uuidv1()
        },
        //Reset graph after confirming an action
        async handleResetGraph(opType) {
            const { SWITCHOVER, FAILOVER, REJOIN } = this.MONITOR_OP_TYPES
            switch (opType) {
                case SWITCHOVER:
                case REJOIN:
                case FAILOVER:
                    this.triggerRerenderNodes()
                    if (opType !== FAILOVER) this.resetDraggingStates()
                    break
            }
        },
        async onConfirm() {
            const { SWITCHOVER, REJOIN } = this.MONITOR_OP_TYPES
            const { MAINTAIN, CLEAR, DRAIN } = this.SERVER_OP_TYPES
            let payload = {
                type: this.confDlg.opType,
                successCb: this.fetchCluster,
            }
            switch (this.confDlg.opType) {
                case SWITCHOVER:
                case REJOIN:
                    this.monitorOpType = this.confDlg.opType
                    await this.manipulateMonitor({
                        type: this.confDlg.opType,
                        successCb: this.monitorOpCallback,
                        id: this.current_cluster.id,
                        opParams: {
                            moduleType: this.current_cluster.module,
                            params: `&${this.draggingStates.draggingNodeId}`,
                        },
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
            this.resetConfDlgStates()
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
        async monitorOpCallback() {
            await this.fetchCluster()
            this.handleResetGraph(this.monitorOpType)
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
