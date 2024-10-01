<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryTab from '@/store/orm/models/QueryTab'
import ConnectionBtn from '@wsComps/ConnectionBtn.vue'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

const props = defineProps({ activeQueryTabConn: { type: Object, required: true } })
const emit = defineEmits(['get-total-btn-width', 'edit-conn', 'add', 'show-user-management'])

const { USER_MANAGEMENT } = QUERY_TAB_TYPE_MAP

const typy = useTypy()

const buttonWrapperRef = ref(null)
const toolbarRightRef = ref(null)

const connectedServerName = computed(() => typy(props.activeQueryTabConn, 'meta.name').safeString)
const hasUserManagementTab = computed(
  () => !typy(QueryTab.query().where('type', USER_MANAGEMENT).first()).isNull
)
const isUserManagementBtnDisabled = computed(
  () => hasUserManagementTab.value || !connectedServerName.value
)

watch(connectedServerName, () => calcWidth())
onMounted(() => calcWidth())

function calcWidth() {
  emit(
    'get-total-btn-width',
    buttonWrapperRef.value.clientWidth + toolbarRightRef.value.clientWidth
  )
}
</script>

<template>
  <div class="d-flex align-center flex-grow-1 border-bottom--table-border">
    <div ref="buttonWrapperRef" class="d-flex align-center px-2">
      <VBtn
        :disabled="$typy(activeQueryTabConn).isEmptyObject"
        class="float-left"
        icon
        variant="text"
        density="compact"
        data-test="add-btn"
        @click="emit('add')"
      >
        <VIcon size="18" color="blue-azure" icon="$mdiPlus" />
      </VBtn>
    </div>
    <div ref="toolbarRightRef" class="ml-auto d-flex align-center px-3 fill-height">
      <TooltipBtn
        icon
        variant="text"
        density="compact"
        class="mr-1"
        :disabled="!connectedServerName"
        data-test="user-management-btn"
        @click="emit('show-user-management')"
      >
        <template #btn-content>
          <VIcon size="18" color="primary" icon="mxs:users" />
        </template>
        {{ $t('userManagement') }}
      </TooltipBtn>
      <ConnectionBtn
        :activeConn="activeQueryTabConn"
        data-test="conn-btn"
        @click="emit('edit-conn')"
      />
      <!-- A slot for SkySQL Query Editor in service details page where the worksheet tab is hidden  -->
      <slot name="query-tab-nav-toolbar-right" />
    </div>
  </div>
</template>
