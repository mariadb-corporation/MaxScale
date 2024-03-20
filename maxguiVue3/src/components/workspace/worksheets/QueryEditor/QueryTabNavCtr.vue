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
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabNavToolbar from '@wkeComps/QueryEditor/QueryTabNavToolbar.vue'
import QueryTabNavItem from '@wkeComps/QueryEditor/QueryTabNavItem.vue'
import { QUERY_CONN_BINDING_TYPES } from '@/constants/workspace'

const props = defineProps({
  queryEditorId: { type: String, required: true },
  activeQueryTabId: { type: String, required: true },
  activeQueryTabConn: { type: Object, required: true },
  queryTabs: { type: Array, required: true },
  height: { type: Number, required: true },
})

const store = useStore()

let queryTabNavToolbarWidth = ref(0)

let activeId = computed({
  get: () => props.activeQueryTabId,
  set: (v) => {
    if (v) QueryEditor.update({ where: props.queryEditorId, data: { active_query_tab_id: v } })
  },
})

async function handleDeleteTab(id) {
  if (props.queryTabs.length === 1) QueryTab.dispatch('refreshLastQueryTab', id)
  else await QueryTab.dispatch('handleDeleteQueryTab', id)
}

function addTab() {
  QueryTab.dispatch('handleAddQueryTab', {
    query_editor_id: props.queryEditorId,
    schema: props.activeQueryTabConn.active_db,
  })
}

function openCnnDlg() {
  store.commit('mxsWorkspace/SET_CONN_DLG', {
    is_opened: true,
    type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR,
  })
}
</script>

<template>
  <div class="d-flex">
    <VTabs
      v-model="activeId"
      show-arrows
      hide-slider
      :height="height"
      class="workspace-tab-style query-tab-nav pagination-btn--small flex-grow-0"
      :style="{ maxWidth: `calc(100% - ${queryTabNavToolbarWidth + 1}px)` }"
      center-active
      mandatory
    >
      <VTab
        v-for="tab in queryTabs"
        :key="tab.id"
        :value="tab.id"
        class="pa-0"
        selected-class="v-tab--selected text-primary"
      >
        <QueryTabNavItem :queryTab="tab" @delete="handleDeleteTab" />
      </VTab>
    </VTabs>
    <QueryTabNavToolbar
      :activeQueryTabConn="activeQueryTabConn"
      :style="{ height: `${height}px` }"
      @add="addTab"
      @edit-conn="openCnnDlg"
      @get-total-btn-width="queryTabNavToolbarWidth = $event"
    >
      <template v-for="(_, name) in $slots" #[name]="slotData">
        <slot :name="name" v-bind="slotData" />
      </template>
    </QueryTabNavToolbar>
  </div>
</template>
