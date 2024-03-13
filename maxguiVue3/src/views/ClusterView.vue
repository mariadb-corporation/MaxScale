<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MXS_OBJ_TYPES, MRDB_MON, MONITOR_OP_TYPES, SERVER_OP_TYPES } from '@/constants'
import MonitorPageHeader from '@/components/details/MonitorPageHeader.vue'
import TreeGraph from '@/components/cluster/TreeGraph.vue'
import ServerNode from '@/components/cluster/ServerNode.vue'
import JoinableServers from '@/components/cluster/JoinableServers.vue'
import { genCluster, genNode } from '@/utils/visualization'
import { useDisplay } from 'vuetify/lib/framework.mjs'

const { SWITCHOVER, REJOIN } = MONITOR_OP_TYPES
const { MAINTAIN, CLEAR, DRAIN } = SERVER_OP_TYPES

const {
  flattenTree,
  lodash: { keyBy },
  uuidv1,
  delay,
} = useHelpers()
const { t } = useI18n()

const store = useStore()
const route = useRoute()
const typy = useTypy()
const { height: windowHeight } = useDisplay()

const DEF_NODE_HEIGHT = 119
const DEF_NODE_WIDTH = 290
const NODE_WRAPPER_CLASS_NAME = 'node-text-wrapper'

let monitorPageHeaderRef = ref(null)
let graphContainerRef = ref(null)
let serverNodeRefs = ref([])
let uniqueKey = ref(uuidv1())
let ctrDim = ref({})
let draggingStates = ref(getDefDraggingStates())
let isCallingOp = ref(false)
let expandedNodes = ref([])
let nodeHeightMap = ref({})
let confDlg = ref(getDefConfDlgState())
let forceClosing = ref(false) // state for MAINTENANCE server option
let transitionDuration = ref(0)

const { fetch: fetchCsStatus, csStatus } = useFetchCsStatus()
const { fetchObj } = useMxsObjActions(MXS_OBJ_TYPES.MONITORS)

const isAdmin = computed(() => store.getters['users/isAdmin'])
const obj_data = computed(() => store.state.monitors.obj_data)
const serverMap = computed(() => store.getters['servers/map'])

const isColumnStoreCluster = computed(
  () => typy(obj_data.value, 'attributes.parameters.cs_admin_api_key').safeString
)
const module = computed(() => typy(obj_data.value, 'attributes.module').safeString)
const state = computed(() => typy(obj_data.value, 'attributes.state').safeString)
const cluster = computed(() => {
  if (module.value === MRDB_MON)
    return genCluster({ monitor: obj_data.value, serverMap: serverMap.value })
  return {}
})

const graphData = computed(() => typy(cluster.value, 'children[0]').safeObjectOrEmpty)
const treeHash = computed(() => {
  let hash = {}
  const getAllItemsPerChildren = (item) => {
    hash[item.id] = item
    if (item.children) return item.children.map((n) => getAllItemsPerChildren(n))
  }
  getAllItemsPerChildren(graphData.value)
  return hash
})
// disable draggable on master node
// root node of graphData is always a master server node
const noDragNodes = computed(() => [graphData.value.id])
const hasExpandedNode = computed(() => Boolean(expandedNodes.value.length))
const maxClusterNodeHeight = computed(() => {
  const v = Math.max(...Object.values(nodeHeightMap.value))
  if (typy(v).isNumber) return v
  return DEF_NODE_HEIGHT
})
const nodeSize = computed(() => {
  return {
    width: DEF_NODE_WIDTH,
    height: hasExpandedNode.value ? maxClusterNodeHeight.value : DEF_NODE_HEIGHT,
  }
})
const expandOnMount = computed(() =>
  Boolean(windowHeight.value >= 1080 && Object.keys(treeHash.value).length <= 4)
)
const confDlgSaveTxt = computed(() => {
  switch (confDlg.value.op.type) {
    case SWITCHOVER:
      return 'promote'
    case REJOIN:
      return 'rejoin'
    case MAINTAIN:
      return 'set'
    default:
      return confDlg.value.op.type
  }
})
const masterNode = computed(() => typy(cluster.value, 'children[0]').safeObjectOrEmpty)
const serverInfo = computed(
  () => typy(obj_data.value, 'attributes.monitor_diagnostics.server_info').safeArray
)
const masterNodeChildren = computed(() => flattenTree(typy(masterNode.value, 'children').safeArray))
const joinableServerNodes = computed(() => {
  const joinableServers = serverInfo.value.filter(
    (s) =>
      s.name !== masterNode.value.name && masterNodeChildren.value.every((n) => n.name !== s.name)
  )
  return joinableServers.map((server) => ({
    id: server.name,
    data: genNode({ server, serverData: serverMap.value[server.name] }),
  }))
})
const standaloneNodeHash = computed(() => keyBy(joinableServerNodes.value, 'id'))

onBeforeMount(async () => await fetchAll())
onMounted(() => nextTick(() => setCtrDim()))

async function fetchAll() {
  await fetchCluster()
  await handleFetchCsStatus()
}

async function handleFetchCsStatus() {
  if (!isCallingOp.value && isColumnStoreCluster.value) {
    await fetchCsStatus({
      id: obj_data.value.id,
      module: module.value,
      state: state.value,
      pollingInterval: 1000,
    })
  }
}

async function fetchCluster() {
  await Promise.all([store.dispatch('servers/fetchAll'), fetchObj(route.params.id)])
}

function setCtrDim() {
  const { clientHeight, clientWidth } = graphContainerRef.value.$el
  ctrDim.value = { width: clientWidth, height: clientHeight }
}

function handleExpandedNode({ type, id }) {
  let target = expandedNodes.value.indexOf(id)
  switch (type) {
    case 'destroy':
      delete expandedNodes[target]
      break
    case 'update':
      if (expandedNodes.value.includes(id)) expandedNodes.value.splice(target, 1)
      else expandedNodes.value.push(id)
      break
  }
}

function getDefDraggingStates() {
  return {
    isDroppable: false,
    droppableTargets: [],
    initialNodeInnerHTML: null,
    draggingNodeId: null,
    droppingNodeId: null,
    nodeCloneClass: 'drag-node-clone',
  }
}

function getDefConfDlgState() {
  return {
    isOpened: false,
    op: { title: '', type: SWITCHOVER, info: '', params: '' },
    targetNode: null,
    opHandler: () => null,
  }
}

/**
 * This helps to store the current innerHTML of the dragging node to initialNodeInnerHTML
 */
function setDefNodeTxt() {
  let cloneEle = document.getElementsByClassName(draggingStates.value.nodeCloneClass)
  if (cloneEle.length) {
    const nodeTxtWrapper = cloneEle[0].getElementsByClassName(NODE_WRAPPER_CLASS_NAME)
    draggingStates.value.initialNodeInnerHTML = nodeTxtWrapper[0].innerHTML
  }
}

/**
 * This finds out which nodes in the cluster that the dragging node can be dropped to
 * @param {Object} node - dragging node to be dropped
 */
function detectDroppableTargets(node) {
  if (node.isMaster) draggingStates.value.droppableTargets = []
  //switchover or rejoin: dragging a slave to a master
  else draggingStates.value.droppableTargets = [masterNode.value.name]
}

/**
 * This helps to change the text content(NODE_WRAPPER_CLASS_NAME) of the dragging node
 * @param {String} type - operation type
 */
function changeNodeTxt(type) {
  let cloneEle = document.getElementsByClassName(draggingStates.value.nodeCloneClass)
  if (cloneEle.length) {
    let nodeTxtWrapper = cloneEle[0].getElementsByClassName(NODE_WRAPPER_CLASS_NAME)
    switch (type) {
      case SWITCHOVER:
      case REJOIN:
        nodeTxtWrapper[0].innerHTML = t(`monitorOps.info.${type}`)
        break
      default:
        nodeTxtWrapper[0].innerHTML = draggingStates.value.initialNodeInnerHTML
        break
    }
  }
}

/**
 *
 * @param {Object} param.draggingNode - dragging node
 * @param {Object} param.droppingNode - dropping node
 * @param {String} param.from - from either TreeGraph (tree) or JoinableServers (standaloneNode)
 */
function detectOperationType({ draggingNode, droppingNode, from }) {
  if (draggingNode.isMaster) confDlg.value.op.Type = ''
  else if (droppingNode.isMaster) {
    switch (from) {
      case 'tree':
        confDlg.value.op.type = SWITCHOVER
        break
      case 'standaloneNode':
        confDlg.value.op.type = REJOIN
    }
  }
  changeNodeTxt(confDlg.value.op.type)
}

/**
 * This helps to change the dragging node's innerHTML back
 * to its initial value. i.e. `initialNodeInnerHTML`
 */
function onDraggingMouseLeave() {
  changeNodeTxt()
  onCancelDrag()
}

function onCancelDrag() {
  draggingStates.value.isDroppable = false
}

/**
 * @param {Object} param.e - drag start event
 * @param {String} param.from - from either TreeGraph (tree) or JoinableServers (standaloneNode)
 */
function onNodeDragStart({ e, from }) {
  document.body.classList.add('cursor--all-move')
  const nodeId = e.item.getAttribute('node_id'),
    node = from === 'standaloneNode' ? standaloneNodeHash.value[nodeId] : treeHash.value[nodeId]
  setDefNodeTxt()
  detectDroppableTargets(node)
}

/**
 * @param {Object} param.e - dragging event
 * @param {String} param.from - from either TreeGraph (tree) or JoinableServers (standaloneNode)
 */
function onNodeDragging({ e, from }) {
  const draggingNodeId = e.dragged.getAttribute('node_id')
  draggingStates.value.draggingNodeId = draggingNodeId
  const draggingNode =
    from === 'standaloneNode'
      ? standaloneNodeHash.value[draggingNodeId]
      : treeHash.value[draggingNodeId]

  const dropEle = e.related // drop target node element
  const droppingNodeId = dropEle.getAttribute('node_id')
  const isDroppable = draggingStates.value.droppableTargets.includes(droppingNodeId)

  if (isDroppable) {
    // listen on the target element
    dropEle.addEventListener('mouseleave', onDraggingMouseLeave)
    const droppingNode = treeHash.value[droppingNodeId]
    detectOperationType({ draggingNode, droppingNode, from })
  } else onCancelDrag()

  draggingStates.value = {
    ...draggingStates.value,
    droppingNodeId,
    isDroppable,
  }
}

function onNodeDragEnd() {
  if (draggingStates.value.isDroppable) {
    switch (confDlg.value.op.type) {
      case SWITCHOVER:
      case REJOIN:
        confDlg.value.op.title = t(`monitorOps.actions.${confDlg.value.op.type}`)
        confDlg.value.targetNode = { id: draggingStates.value.draggingNodeId }
        break
    }
    confDlg.value.isOpened = true
    transitionDuration.value = 1500
  }
  draggingStates.value.droppableTargets = []
  document.body.classList.remove('cursor--all-move')
}

async function onConfirm() {
  switch (confDlg.value.op.type) {
    case SWITCHOVER:
    case REJOIN:
      await monitorPageHeaderRef.value.opHandler({
        op: confDlg.value.op,
        id: cluster.value.id,
        module: module.value,
        opParams: {
          module: module.value,
          params: `&${draggingStates.value.draggingNodeId}`,
        },
        successCb: opSuccessCb,
      })
      break
    case DRAIN:
    case CLEAR:
    case MAINTAIN:
      await confDlg.value.opHandler({
        op: confDlg.value.op,
        id: confDlg.value.targetNode.id,
        forceClosing: forceClosing.value,
        successCb: opSuccessCb,
      })
      break
  }
  confDlg.value = getDefConfDlgState()
}

function onChooseOp({ op, target: { id }, opHandler }) {
  confDlg.value = { isOpened: true, op, targetNode: { id }, opHandler }
}

/**
 * A node can be expanded or collapsed by the user's interaction, so the
 * height of each node is dynamic.
 * This method should be called when the order of the nodes has changed.
 * e.g. after a switchover or a rejoin action.
 * This helps to get the accurate height of each node because
 * the `nodeHeightMap` state won't be updated if the `ServerNode`
 * is not re-rendered.
 */
function triggerRerenderNodes() {
  uniqueKey.value = uuidv1()
}

async function opSuccessCb() {
  await fetchAll()
  draggingStates.value = getDefDraggingStates()
  triggerRerenderNodes()
}

async function cleanUpTransition() {
  await delay(transitionDuration.value)
  transitionDuration.value = 0
}
</script>
<template>
  <ViewWrapper :overflow="false" fluid class="fill-height">
    <MonitorPageHeader
      ref="monitorPageHeaderRef"
      :item="obj_data"
      :successCb="opSuccessCb"
      :onCountDone="fetchAll"
      :csStatus="csStatus"
      :fetchCsStatus="fetchCsStatus"
      :showGlobalSearch="false"
      @is-calling-op="isCallingOp = $event"
    >
      <template #page-title="{ pageId }">
        <RouterLink :to="`/dashboard/monitors/${pageId}`" class="rsrc-link">
          {{ pageId }}
        </RouterLink>
      </template>
    </MonitorPageHeader>
    <VCard
      ref="graphContainerRef"
      v-resize.quiet="setCtrDim"
      class="ml-6 mt-9 fill-height"
      flat
      border
    >
      <TreeGraph
        v-if="ctrDim.height && !$typy(graphData).isEmptyObject"
        :data="graphData"
        :dim="ctrDim"
        :nodeSize="nodeSize"
        :draggable="isAdmin"
        :draggableGroup="{
          name: 'TreeGraph',
          put: ['JoinableServers'], // allow nodes on JoinableServers to be dragged here
        }"
        :noDragNodes="noDragNodes"
        :expandedNodes="expandedNodes"
        :nodeHeightMap="nodeHeightMap"
        :cloneClass="draggingStates.nodeCloneClass"
        :transitionDuration="transitionDuration"
        @on-node-drag-start="onNodeDragStart({ e: $event, from: 'tree' })"
        @on-node-dragging="onNodeDragging({ e: $event, from: 'tree' })"
        @on-node-drag-end="onNodeDragEnd"
      >
        <template #default="{ data: { node } }">
          <ServerNode
            v-if="!$typy(node, 'data').isEmptyObject"
            ref="serverNodeRefs"
            :key="`${uniqueKey}-${node.id}`"
            :node="node"
            :droppableTargets="draggingStates.droppableTargets"
            :bodyWrapperClass="NODE_WRAPPER_CLASS_NAME"
            :expandOnMount="expandOnMount"
            class="fill-height"
            @get-expanded-node="handleExpandedNode"
            @node-height="nodeHeightMap[node.id] = $event"
            @on-choose-op="onChooseOp"
          />
        </template>
      </TreeGraph>
      <JoinableServers
        v-if="joinableServerNodes.length"
        :data="joinableServerNodes"
        :draggableGroup="{ name: 'JoinableServers' }"
        :cloneClass="draggingStates.nodeCloneClass"
        :bodyWrapperClass="NODE_WRAPPER_CLASS_NAME"
        :droppableTargets="draggingStates.droppableTargets"
        :dim="ctrDim"
        :draggable="isAdmin"
        @on-choose-op="onChooseOp"
        @on-drag-start="onNodeDragStart({ e: $event, from: 'standaloneNode' })"
        @on-dragging="onNodeDragging({ e: $event, from: 'standaloneNode' })"
        @on-drag-end="onNodeDragEnd"
      />
    </VCard>
    <ConfirmDlg
      v-model="confDlg.isOpened"
      :title="confDlg.op.title"
      :saveText="confDlgSaveTxt"
      :type="confDlg.op.type"
      :item="confDlg.targetNode"
      :smallInfo="confDlg.op.info"
      :onSave="onConfirm"
      @after-close="cleanUpTransition"
    >
      <template v-if="confDlg.op.type === MAINTAIN" #body-append>
        <VCheckbox
          v-model="forceClosing"
          class="mt-2 mb-4"
          :label="$t('forceClosing')"
          color="primary"
          hide-details
          density="comfortable"
        />
      </template>
    </ConfirmDlg>
  </ViewWrapper>
</template>
