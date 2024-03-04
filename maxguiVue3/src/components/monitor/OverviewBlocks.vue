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
import { MXS_OBJ_TYPES, MONITOR_OP_TYPES } from '@/constants'
import SelDlg from '@/components/details/SelDlg.vue'

const props = defineProps({
  item: { type: Object, required: true },
  module: { type: String, required: true },
  onSwitchoverSuccess: { type: Function, required: true },
})
const typy = useTypy()

let showEditBtn = ref(false)
let chosenServerItem = ref([])
let isSelectDlgOpened = ref(false)

let initialServerItem = ref({})

const state = computed(() => typy(props.item, 'attributes.state').safeString)
const { map: allOps, handler: opHandler } = useMonitorOpMap(state)

const getTopOverviewInfo = computed(() => {
  const {
    attributes: {
      monitor_diagnostics: { master, master_gtid_domain_id, state, primary } = {},
    } = {},
  } = props.item
  return {
    master,
    master_gtid_domain_id,
    state,
    primary,
  }
})

const serverIds = computed(() => {
  const { relationships: { servers: { data: serversData = [] } = {} } = {} } = props.item
  return serversData.map((server) => ({ id: server.id, type: server.type }))
})

const switchoverOp = computed(() => allOps.value[MONITOR_OP_TYPES.SWITCHOVER])

function onDlgOpened() {
  if (!getTopOverviewInfo.value.master) initialServerItem.value = null
  else initialServerItem.value = { id: getTopOverviewInfo.value.master, type: 'servers' }
}

async function confirmChange() {
  const newMasterId = typy(chosenServerItem.value, '[0].id').safeString
  await opHandler({
    op: switchoverOp.value,
    id: props.item.id,
    opParams: { module: props.module, params: `&${newMasterId}` },
    module: props.module,
    successCb: props.onSwitchoverSuccess,
  })
}
</script>

<template>
  <VSheet class="d-flex mb-2">
    <OutlinedOverviewCard
      v-for="(value, name) in getTopOverviewInfo"
      :key="name"
      wrapperClass="mt-0"
      :class="`card-${name} px-10`"
      :hoverableCard="name === 'master'"
      @is-hovered="showEditBtn = $event"
    >
      <template #card-body>
        <span class="text-caption text-uppercase font-weight-bold text-deep-ocean">
          {{ name.replace('_', ' ') }}
        </span>
        <template v-if="name === 'master'">
          <RouterLink
            v-if="value"
            :to="`/dashboard/servers/${value}`"
            class="text-no-wrap text-body-2 rsrc-link"
          >
            <span>{{ value }} </span>
          </RouterLink>
          <span v-else class="text-no-wrap text-body-2">
            {{ String(value) }}
          </span>
          <TooltipBtn
            density="comfortable"
            icon
            variant="text"
            class="absolute"
            :style="{
              right: '10px',
              bottom: '10px',
            }"
            @click="isSelectDlgOpened = true"
          >
            <template #btn-content>
              <VIcon size="18" color="primary" icon="mxs:edit" v-show="showEditBtn" />
            </template>
            {{ switchoverOp.title }}
          </TooltipBtn>
        </template>
        <span v-else class="text-no-wrap text-body-2">
          {{ String(value) }}
        </span>
      </template>
    </OutlinedOverviewCard>
    <!-- TODO: Use the SWITCHOVER option in <MonitorPageHeader/> after the migration -->
    <SelDlg
      v-model="isSelectDlgOpened"
      :title="switchoverOp.title"
      saveText="swap"
      :type="MXS_OBJ_TYPES.SERVERS"
      :items="serverIds"
      :initialValue="initialServerItem"
      :onSave="confirmChange"
      @selected-items="chosenServerItem = $event"
      @on-open="onDlgOpened"
    >
      <template #body-append>
        <small class="d-inline-block mt-4"> {{ $t('info.switchover') }} </small>
      </template>
    </SelDlg>
  </VSheet>
</template>
