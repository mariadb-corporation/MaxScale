<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import InsightViewer from '@wsModels/InsightViewer'
import InsightViewerTabItem from '@wkeComps/QueryEditor/InsightViewerTabItem.vue'
import queryConnService from '@wsServices/queryConnService'
import { NODE_TYPES, INSIGHT_SPECS } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const tabNavHeight = 24
const typy = useTypy()
const {
  lodash: { pickBy, pick },
} = useHelpers()

const insightViewer = computed(() => InsightViewer.find(props.queryTab.id) || {})
const activeSpec = computed({
  get: () => typy(insightViewer.value, 'active_spec').safeString,
  set: (v) => InsightViewer.update({ where: props.queryTab.id, data: { active_spec: v } }),
})
const queryTabConn = computed(() => queryConnService.findQueryTabConn(props.queryTab.id))
const node = computed(() => typy(insightViewer.value, 'active_node').safeObjectOrEmpty)
const nodeType = computed(() => typy(node.value, 'type').safeString)
const specs = computed(() => {
  switch (nodeType.value) {
    case NODE_TYPES.SCHEMA:
      return pickBy(INSIGHT_SPECS, (v, key) => key !== 'CREATION_INFO')
    case NODE_TYPES.TBL:
      return pick(INSIGHT_SPECS, ['COLUMNS', 'INDEXES', 'TRIGGERS', 'DDL'])
    case NODE_TYPES.VIEW:
    case NODE_TYPES.TRIGGER:
    case NODE_TYPES.SP:
    case NODE_TYPES.FN:
      return pick(INSIGHT_SPECS, ['CREATION_INFO', 'DDL'])
    default:
      return {}
  }
})
const tabItemDim = computed(() => ({
  width: props.dim.width - 40, // minus px-5 padding
  height: props.dim.height - tabNavHeight - 8, // minus pt-2  padding
}))
</script>

<template>
  <VCard class="insight-viewer no-shadow fill-height" tile>
    <VTabs v-if="!$typy(specs).isEmptyObject" v-model="activeSpec" :height="tabNavHeight">
      <VTab v-for="spec of specs" :key="spec" :value="spec" class="text-primary">
        {{ spec }}
      </VTab>
    </VTabs>
    <div :style="{ height: `${tabItemDim.height}px` }" class="pt-2 px-5 text-small-text">
      <InsightViewerTabItem
        v-if="queryTabConn.id && node.id"
        :dim="tabItemDim"
        :conn="queryTabConn"
        :node="node"
        :activeSpec="activeSpec"
        :specs="specs"
        :nodeType="nodeType"
      />
    </div>
  </VCard>
</template>
