<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { QUERY_CONN_BINDING_TYPES } from '@/constants/workspace'
import { MXS_OBJ_TYPES, SERVER_OP_TYPES } from '@/constants'
import GraphNode from '@/components/cluster/GraphNode.vue'
import OperationsList from '@/components/details/OperationsList.vue'

const props = defineProps({
  node: { type: Object, required: true },
  droppableTargets: { type: Array, required: true },
})

const emit = defineEmits([
  'on-choose-op', // { op: object, target: object, opHandler: function }
])

const { SERVERS } = MXS_OBJ_TYPES
const store = useStore()
const typy = useTypy()
const router = useRouter()
const { getMin, uptimeHumanize, getMostFreq } = useHelpers()

const lineHeight = '18px'

const isAdmin = computed(() => store.getters['users/isAdmin'])
const nodeAttrs = computed(() => typy(props.node, 'data.serverData.attributes').safeObject)
const state = computed(() => nodeAttrs.value.state)
const isDroppableNode = computed(() => props.droppableTargets.includes(props.node.id))
const iconColorName = computed(() => (isDroppableNode.value ? 'white' : 'primary'))

const { computedMap: computedServerOpMap, handler: opHandler } = useServerOpMap(state)

// only slave node has this property
const slave_connections = computed(
  () => typy(props.node.data, 'server_info.slave_connections').safeArray
)
const sbm = computed(() =>
  getMin({ arr: slave_connections.value, pickBy: 'seconds_behind_master' })
)

const firstSlideCommonInfo = computed(() => ({
  last_event: nodeAttrs.value.last_event,
  gtid_binlog_pos: nodeAttrs.value.gtid_binlog_pos,
  gtid_current_pos: nodeAttrs.value.gtid_current_pos,
}))

const secondSlideCommonInfo = computed(() => ({
  uptime: uptimeHumanize(nodeAttrs.value.uptime),
  version_string: nodeAttrs.value.version_string,
}))

const masterExtraInfo = computed(() => [firstSlideCommonInfo.value, secondSlideCommonInfo.value])

const slaveExtraInfo = computed(() => [
  {
    ...firstSlideCommonInfo.value,
    slave_io_running: getMostFreq({
      arr: slave_connections.value,
      pickBy: 'slave_io_running',
    }),
    slave_sql_running: getMostFreq({
      arr: slave_connections.value,
      pickBy: 'slave_sql_running',
    }),
  },
  secondSlideCommonInfo.value,
])

const isMaster = computed(() => props.node.data.isMaster)

const extraInfo = computed(() => (isMaster.value ? masterExtraInfo.value : slaveExtraInfo.value))

const extraInfoSlides = computed(() => extraInfo.value)

const operationMatrix = computed(() => {
  const { MAINTAIN, CLEAR, DRAIN } = SERVER_OP_TYPES
  const serverOpMap = computedServerOpMap.value
  return [[serverOpMap[MAINTAIN], serverOpMap[CLEAR], serverOpMap[DRAIN]]]
})

/**
 * Check if there is a QueryEditor connected to the provided conn_name and set it as the
 * active worksheet. Otherwise, find an blank worksheet, set it as active show connection
 * dialog with pre-select object
 * @param {String} param.conn_name - connection name
 */
async function chooseQueryEditorWke({ type, conn_name }) {
  const queryEditorConns = QueryConn.getters('queryEditorConns')
  // Find connection
  const queryEditorConn = queryEditorConns.find(
    (c) => typy(c, 'meta.name').safeString === conn_name
  )
  /**
   * If it is already bound to a QueryEditor, use the QueryEditor id for
   * setting active worksheet because the QueryEditor id is also Worksheet id.
   */
  if (typy(queryEditorConn, 'query_editor_id').safeString)
    Worksheet.commit((state) => (state.active_wke_id = queryEditorConn.query_editor_id))
  else {
    const blankQueryEditorWke = Worksheet.query()
      .where(
        (w) =>
          typy(w, 'etl_task_id').isNull &&
          typy(w, 'query_editor_id').isNull &&
          typy(w, 'erd_task_id').isNull
      )
      .first()
    // Use a blank query editor wke if there is one, otherwise create a new one
    if (blankQueryEditorWke)
      Worksheet.commit((state) => (state.active_wke_id = blankQueryEditorWke.id))
    else Worksheet.dispatch('insertQueryEditorWke')
    store.commit('queryConnsMem/SET_PRE_SELECT_CONN_RSRC', { type, id: conn_name })
    store.commit('mxsWorkspace/SET_CONN_DLG', {
      is_opened: true,
      type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR,
    })
  }
}
async function onClickRunQueries() {
  await chooseQueryEditorWke({ type: SERVERS, conn_name: props.node.id })
  router.push(`/workspace`)
}
</script>

<template>
  <GraphNode
    v-if="nodeAttrs"
    :node="node"
    :lineHeight="lineHeight"
    :extraInfoSlides="extraInfoSlides"
    class="server-node"
  >
    <template #node-heading>
      <div
        class="node-heading d-flex align-center flex-row px-3 py-1"
        :class="{ 'node-heading__droppable': isDroppableNode }"
      >
        <VIcon
          size="16"
          class="mr-1"
          :color="isDroppableNode ? 'white' : isMaster ? 'navigation' : 'accent'"
          :icon="isMaster ? 'mxs:primaryServer' : 'mxs:secondaryServer'"
        />
        <RouterLink
          target="_blank"
          rel="noopener noreferrer"
          :to="`/dashboard/servers/${node.id}`"
          class="text-truncate rsrc-link"
        >
          {{ node.id }}
        </RouterLink>
        <VSpacer />
        <span class="readonly-val ml-1 text-grayed-out font-weight-medium text-no-wrap">
          {{ nodeAttrs.read_only ? $t('readonly') : $t('writable') }}
        </span>
        <div class="ml-1 button-container text-no-wrap">
          <TooltipBtn
            size="small"
            icon
            variant="text"
            density="comfortable"
            @click="onClickRunQueries"
          >
            <template #btn-content>
              <VIcon size="16" :color="iconColorName" icon="mxs:workspace" />
            </template>
            {{ $t('runQueries') }}
          </TooltipBtn>

          <VMenu
            v-if="isAdmin"
            content-class="full-border rounded bg-background"
            transition="slide-y-transition"
            offset="4"
          >
            <template #activator="{ props }">
              <VBtn size="small" icon variant="text" density="comfortable" v-bind="props">
                <VIcon size="16" :color="iconColorName" icon="mxs:settings" />
              </VBtn>
            </template>
            <OperationsList
              :data="operationMatrix"
              :handler="(op) => emit('on-choose-op', { op, target: node, opHandler })"
            />
          </VMenu>
        </div>
      </div>
    </template>
    <template #node-body>
      <div class="d-flex flex-row flex-grow-1 text-capitalize" :style="{ lineHeight }">
        <span class="sbm mr-2 font-weight-bold">
          {{ $t('state') }}
        </span>
        <StatusIcon size="16" class="mr-1" :type="SERVERS" :value="nodeAttrs.state" />
        <GblTooltipActivator
          :data="{ txt: String(nodeAttrs.state) }"
          :debounce="0"
          activateOnTruncation
          :maxWidth="150"
        />
        <VSpacer />
        <span v-if="!node.data.isMaster" class="ml-1">
          <span class="font-weight-bold text-capitalize pr-1">{{ $t('lag') }} </span>
          <span> {{ sbm }}s </span>
        </span>
      </div>
      <div class="d-flex flex-grow-1" :style="{ lineHeight }">
        <span class="text-capitalize font-weight-bold mr-2">
          {{ $t('connections', 2) }}
        </span>
        <span>{{ nodeAttrs.statistics.connections }} </span>
      </div>
      <div class="d-flex flex-grow-1" :style="{ lineHeight }">
        <span class="text-capitalize font-weight-bold mr-2">
          {{ nodeAttrs.parameters.socket ? $t('socket') : $t('address') }}
        </span>

        <GblTooltipActivator
          :data="{ txt: String($helpers.getAddress(nodeAttrs.parameters)) }"
          :debounce="0"
          activateOnTruncation
        />
      </div>
    </template>
  </GraphNode>
</template>

<style lang="scss" scoped>
.server-node {
  .v-icon {
    transition: none;
  }
  .node-heading {
    &__droppable {
      background: colors.$success;
      color: white;
      .rsrc-link {
        color: white !important;
      }
      .readonly-val {
        color: white !important;
      }
    }
  }
}
</style>
