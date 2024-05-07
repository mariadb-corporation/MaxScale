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
import EtlTaskManage from '@wkeComps/DataMigration/EtlTaskManage.vue'
import etlTaskService from '@wsServices/etlTaskService'
import queryConnService from '@wsServices/queryConnService'
import { ETL_ACTIONS, ETL_STATUS } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })
const props = defineProps({ task: { type: Object, required: true } })

const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART } = ETL_ACTIONS
const ACTION_TYPES = [CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART]

const { t } = useI18n()

const isMenuOpened = ref(false)
const isQuickActionBtnDisabled = ref(false)

const hasNoConn = computed(() => queryConnService.findEtlConns(props.task.id).length === 0)
const isRunning = computed(() => props.task.status === ETL_STATUS.RUNNING)
// No longer able to do anything else except deleting the task
const isDone = computed(() => hasNoConn.value && props.task.status === ETL_STATUS.COMPLETE)
const shouldShowQuickActionBtn = computed(() => isRunning.value || isDone.value)
const quickActionBtnData = computed(() => {
  let type
  if (isRunning.value) type = ETL_ACTIONS.CANCEL
  else if (isDone.value) type = ETL_ACTIONS.DELETE
  return { type, txt: t(`etlOps.actions.${type}`) }
})

async function quickActionHandler() {
  isQuickActionBtnDisabled.value = true
  await etlTaskService.actionHandler({ type: quickActionBtnData.value.type, task: props.task })
  isQuickActionBtnDisabled.value = false
}
</script>

<template>
  <VBtn
    v-if="shouldShowQuickActionBtn"
    :height="30"
    :color="quickActionBtnData.type === ETL_ACTIONS.DELETE ? 'error' : 'primary'"
    rounded
    variant="outlined"
    class="ml-4 font-weight-medium px-4 text-capitalize"
    :disabled="isQuickActionBtnDisabled"
    data-test="quick-action-btn"
    @click="quickActionHandler"
  >
    {{ quickActionBtnData.txt }}
  </VBtn>
  <EtlTaskManage v-else v-model="isMenuOpened" :task="task" :types="ACTION_TYPES" v-bind="$attrs">
    <template #activator="{ props }">
      <VBtn
        :height="30"
        color="primary"
        rounded
        variant="outlined"
        class="ml-4 font-weight-medium px-4 text-capitalize"
        v-bind="props"
      >
        {{ $t('manage') }}
        <VIcon
          :class="[isMenuOpened ? 'rotate-up' : 'rotate-down']"
          size="14"
          class="mr-0 ml-1"
          icon="mxs:arrowDown"
        />
      </VBtn>
    </template>
  </EtlTaskManage>
</template>
