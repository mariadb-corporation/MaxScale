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
import ConnectionBtn from '@wsComps/ConnectionBtn.vue'

const props = defineProps({ activeQueryTabConn: { type: Object, required: true } })
const emit = defineEmits(['get-total-btn-width', 'edit-conn', 'add'])

const typy = useTypy()

let buttonWrapperRef = ref(null)
let toolbarRightRef = ref(null)

const connectedServerName = computed(() => typy(props.activeQueryTabConn, 'meta.name').safeString)

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
  <div class="d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border">
    <div ref="buttonWrapperRef" class="d-flex align-center px-2">
      <VBtn
        :disabled="$typy(activeQueryTabConn).isEmptyObject"
        class="float-left"
        icon
        variant="text"
        density="compact"
        @click="emit('add')"
      >
        <VIcon size="18" color="blue-azure" icon="$mdiPlus" />
      </VBtn>
    </div>
    <div ref="toolbarRightRef" class="ml-auto d-flex align-center px-3 fill-height">
      <ConnectionBtn :activeConn="activeQueryTabConn" @click="emit('edit-conn')" />
      <!-- A slot for SkySQL Query Editor in service details page where the worksheet tab is hidden  -->
      <slot name="query-tab-nav-toolbar-right" />
    </div>
  </div>
</template>
