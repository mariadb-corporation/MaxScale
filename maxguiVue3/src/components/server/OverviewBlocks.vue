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
import SelDlg from '@/components/details/SelDlg.vue'
import statusIconHelpers from '@/utils/statusIconHelpers'
import { MXS_OBJ_TYPES } from '@/constants'

const props = defineProps({
  item: { type: Object, required: true },
  handlePatchRelationship: { type: Function, required: true },
})

const { t } = useI18n()
const fetchObjData = useFetchObjData()

const showEditBtn = ref(false)
const dialogTitle = ref('')
const targetItem = ref(null)
// Select dialog
const isSelectDlgOpened = ref(false)
const targetSelectItemType = ref(MXS_OBJ_TYPES.MONITORS)
const itemsList = ref([])
const initialValue = ref(null)
const valueClass = 'detail-overview__card__value text-no-wrap text-body-2'

const getTopOverviewInfo = computed(() => {
  const {
    attributes: {
      state,
      last_event,
      triggered_at,
      parameters: { address, socket, port } = {},
    } = {},
    relationships: { monitors } = {},
  } = props.item
  let overviewInfo = {
    address,
    socket,
    port,
    state,
    last_event,
    triggered_at,
    monitor: monitors ? monitors.data[0].id : 'undefined',
  }
  if (socket) {
    delete overviewInfo.address
    delete overviewInfo.port
  } else delete overviewInfo.socket
  return overviewInfo
})

const serverStateClass = computed(() => {
  switch (statusIconHelpers[MXS_OBJ_TYPES.SERVERS](getTopOverviewInfo.value.state)) {
    case 0:
      return 'text-error'
    case 1:
      return 'text-success'
    default:
      return 'text-grayed-out'
  }
})

function onEdit(type) {
  dialogTitle.value = `${t(`changeEntity`, { entityName: t(type, 1) })}`
  if (type === MXS_OBJ_TYPES.MONITORS) targetSelectItemType.value = type
  isSelectDlgOpened.value = true
}

async function getAllEntities() {
  if (targetSelectItemType.value === MXS_OBJ_TYPES.MONITORS) {
    const data = await fetchObjData({
      type: targetSelectItemType.value,
    })
    itemsList.value = data.map((monitor) => ({ id: monitor.id, type: monitor.type }))
    const { monitor: id } = getTopOverviewInfo.value
    if (id === 'undefined') initialValue.value = null
    else initialValue.value = { id, type: MXS_OBJ_TYPES.MONITORS }
  }
}
async function confirmChange() {
  if (targetSelectItemType.value === MXS_OBJ_TYPES.MONITORS)
    await props.handlePatchRelationship({
      type: targetSelectItemType.value,
      data: targetItem.value,
    })
}
</script>

<template>
  <VSheet class="d-flex mb-2">
    <OutlinedOverviewCard
      v-for="(value, name, index) in getTopOverviewInfo"
      :key="name"
      wrapperClass="mt-5"
      class="px-10 rounded-0"
      :hoverableCard="name === 'monitor'"
      @is-hovered="showEditBtn = $event"
    >
      <template #title>
        <span :style="{ visibility: index === 0 ? 'visible' : 'hidden' }">
          {{ $t('overview') }}
        </span>
      </template>
      <template #card-body>
        <span
          class="detail-overview__card__name text-caption text-uppercase font-weight-bold text-deep-ocean"
        >
          {{ name.replace('_', ' ') }}
        </span>
        <template v-if="name === 'monitor'">
          <RouterLink
            v-if="value !== 'undefined'"
            :key="index"
            :to="`/dashboard/monitors/${value}`"
            :class="[valueClass, 'rsrc-link']"
          >
            <span>{{ value }} </span>
          </RouterLink>
          <span v-else :class="valueClass">
            {{ value }}
          </span>
          <VBtn
            v-if="showEditBtn"
            class="monitor-edit-btn absolute"
            density="comfortable"
            variant="text"
            icon
            @click="() => onEdit('monitors')"
          >
            <VIcon size="18" color="primary" icon="mxs:edit" />
          </VBtn>
        </template>
        <span v-else-if="name === 'state'" :class="valueClass">
          <template v-if="value.indexOf(',') > 0">
            <span class="font-weight-bold" :class="[serverStateClass]">
              {{ value.slice(0, value.indexOf(',')) }}
            </span>
            /
            <span class="font-weight-bold" :class="[serverStateClass]">
              {{ value.slice(value.indexOf(',') + 1) }}
            </span>
          </template>
          <span v-else class="font-weight-bold" :class="[serverStateClass]">
            {{ value }}
          </span>
        </span>
        <span v-else :class="valueClass">
          {{ name === 'triggered_at' && value ? $helpers.dateFormat({ value }) : String(value) }}
        </span>
      </template>
    </OutlinedOverviewCard>
    <SelDlg
      v-model="isSelectDlgOpened"
      :title="dialogTitle"
      saveText="change"
      :type="targetSelectItemType"
      clearable
      :items="itemsList"
      :initialValue="initialValue"
      :onSave="confirmChange"
      @selected-items="targetItem = $event"
      @on-open="getAllEntities"
    />
  </VSheet>
</template>

<style lang="scss" scoped>
.monitor-edit-btn {
  right: 10px;
  bottom: 10px;
}
</style>
