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
import etlTaskService from '@wsServices/etlTaskService'
import queryConnService from '@wsServices/queryConnService'
import { ETL_ACTION_MAP, ETL_STATUS_MAP } from '@/constants/workspace'

const props = defineProps({
  task: { type: Object, required: true },
  types: { type: Array, required: true },
})
const emit = defineEmits(['on-restart'])

const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART } = ETL_ACTION_MAP
const { INITIALIZING, RUNNING, COMPLETE } = ETL_STATUS_MAP

const { t } = useI18n()

const ACTION_MAP = Object.keys(ETL_ACTION_MAP).reduce((obj, key) => {
  const value = ETL_ACTION_MAP[key]
  obj[value] = {
    title: t(`etlOps.actions.${value}`),
    type: value,
  }
  return obj
}, {})

const hasNoConn = computed(() => queryConnService.findEtlConns(props.task.id).length === 0)
const isRunning = computed(() => props.task.status === RUNNING)
const actions = computed(() => {
  const types = Object.values(ACTION_MAP).filter((o) => props.types.includes(o.type))
  const status = props.task.status
  return types.map((o) => {
    let disabled = false
    switch (o.type) {
      case CANCEL:
        disabled = !isRunning.value
        break
      case DELETE:
        disabled = isRunning.value
        break
      case DISCONNECT:
      case MIGR_OTHER_OBJS:
        disabled = isRunning.value || hasNoConn.value
        break
      case RESTART:
        disabled = isRunning.value || status === COMPLETE || status === INITIALIZING
        break
    }
    return { ...o, disabled }
  })
})

async function handler(type) {
  if (type === RESTART) emit('on-restart', props.task.id)
  else await etlTaskService.actionHandler({ type, task: props.task })
}
</script>

<template>
  <VMenu content-class="with-shadow-border--none">
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
    <VList>
      <VListItem
        v-for="action in actions"
        :key="action.title"
        :disabled="action.disabled"
        @click="handler(action.type)"
      >
        <template #title>
          <span :class="[action.type === ETL_ACTION_MAP.DELETE ? 'text-error' : 'text-text']">
            {{ action.title }}
          </span>
        </template>
      </VListItem>
    </VList>
  </VMenu>
</template>
