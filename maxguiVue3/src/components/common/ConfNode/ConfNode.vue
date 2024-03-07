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
import FilterNodes from '@/components/common/ConfNode/FilterNodes.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const props = defineProps({
  node: { type: Object, required: true },
  nodeWidth: { type: Number, default: 200 },
  onNodeResized: { type: Function, default: () => null },
  showFiltersInService: { type: Boolean, default: false },
})

let isVisualizingFilters = ref(false)
const lineHeight = '18px'
const { MONITORS, SERVERS, SERVICES, FILTERS, LISTENERS } = MXS_OBJ_TYPES
const typy = useTypy()
const { getAddress } = useHelpers()

const headingColor = computed(() => {
  switch (nodeType.value) {
    case MONITORS:
      return { bg: '#0E9BC0', txt: '#fff' }
    case SERVERS:
      return { bg: '#e7eef1', txt: '#2d9cdb' }
    case SERVICES:
      return { bg: '#7dd012', txt: '#fff' }
    case FILTERS:
      return { bg: '#f59d34', txt: '#fff' }
    case LISTENERS:
      return { bg: '#424f62', txt: '#fff' }
    default:
      return { bg: '#e7eef1', txt: '#fff' }
  }
})
const nodeData = computed(() => typy(props.node, 'nodeData').safeObjectOrEmpty)
const nodeType = computed(() => typy(props.node, 'type').safeString)
// for node type SERVICES
const filters = computed(() => typy(nodeData.value, 'relationships.filters.data').safeArray)
const isServiceWithFiltersNode = computed(
  () => nodeType.value === SERVICES && Boolean(filters.value.length)
)
const isShowingFilterNodes = computed(
  () => props.showFiltersInService && isServiceWithFiltersNode.value && isVisualizingFilters.value
)
const nodeBody = computed(() => {
  switch (nodeType.value) {
    case MONITORS: {
      const { state, module } = nodeData.value.attributes
      return { state, module }
    }
    case SERVERS: {
      const { state, parameters = {}, statistics: { connections } = {} } = nodeData.value.attributes
      let data = {
        state,
        connections,
        [parameters.socket ? 'socket' : 'address']: getAddress(parameters),
      }
      return data
    }
    case SERVICES: {
      const { state, router, total_connections } = nodeData.value.attributes
      let body = {
        state,
        router,
        'Total Connections': total_connections,
      }
      if (!isVisualizingFilters.value && filters.value.length)
        body.filters = filters.value.map((f) => f.id).join(', ')
      return body
    }
    case FILTERS:
      return { module: typy(nodeData.value, 'attributes.module').safeString }
    case LISTENERS: {
      const { state, parameters = {} } = nodeData.value.attributes
      return {
        state,
        address: getAddress(parameters),
        protocol: parameters.protocol,
        authenticator: parameters.authenticator,
      }
    }
    default:
      return {}
  }
})

onMounted(() => {
  if (filters.value.length < 4) isVisualizingFilters.value = true
})

function handleVisFilters() {
  isVisualizingFilters.value = !isVisualizingFilters.value
  props.onNodeResized(props.node.id)
}
</script>

<template>
  <VCard
    flat
    border
    class="node-card fill-height relative"
    :style="{ borderColor: headingColor.bg }"
  >
    <div
      class="node-heading d-flex align-center justify-center flex-row px-3 py-1"
      :style="{ backgroundColor: headingColor.bg }"
    >
      <!-- Don't render service id here if the filter nodes are visualizing, render it in the body
           that has filters nodes point to the service node.
      -->
      <template v-if="!isShowingFilterNodes">
        <RouterLink
          target="_blank"
          rel="noopener noreferrer"
          :to="`/dashboard/${nodeType}/${node.id}`"
          class="text-truncate mr-2"
          :style="{ color: headingColor.txt }"
        >
          {{ node.id }}
        </RouterLink>
        <VSpacer />
      </template>
      <span
        class="node-type font-weight-medium text-no-wrap text-uppercase"
        :style="{ color: headingColor.txt }"
      >
        {{ $t(nodeType, 1) }}
      </span>
    </div>
    <FilterNodes
      v-if="showFiltersInService && isServiceWithFiltersNode && isVisualizingFilters"
      :filters="filters"
      :handleVisFilters="handleVisFilters"
      :style="{ width: `${nodeWidth}px` }"
    />
    <div
      class="text-navigation d-flex justify-center flex-column px-3 py-1"
      :class="{ 'mx-5': isShowingFilterNodes }"
      :style="{ border: isShowingFilterNodes ? `1px solid ${headingColor.bg}` : 'unset' }"
    >
      <template v-if="isShowingFilterNodes">
        <RouterLink
          target="_blank"
          rel="noopener noreferrer"
          :to="`/dashboard/${node.type}/${node.id}`"
          class="text-truncate"
        >
          {{ node.id }}
        </RouterLink>
      </template>
      <div
        v-for="(value, key) in nodeBody"
        :key="key"
        class="d-flex flex-row flex-grow-1 align-center"
        :style="{ lineHeight }"
      >
        <span class="mr-2 font-weight-bold text-capitalize text-no-wrap">
          {{ key }}
        </span>
        <StatusIcon v-if="key === 'state'" size="13" class="mr-1" :type="nodeType" :value="value" />
        <GblTooltipActivator activateOnTruncation :data="{ txt: String(value) }" :debounce="0" />
        <TooltipBtn
          v-if="key === 'filters'"
          class="ml-auto vis-filter-btn"
          size="small"
          icon
          variant="text"
          @click="handleVisFilters"
        >
          <template #btn-content>
            <VIcon size="14" color="primary" icon="mxs:reports" />
          </template>
          {{ $t('visFilters') }}
        </TooltipBtn>
      </div>
    </div>
  </VCard>
</template>

<style lang="scss" scoped>
.node-card {
  font-size: 12px;
}
</style>
