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
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import etlTaskService from '@wsServices/etlTaskService'
import { ETL_CREATE_MODES } from '@/constants/workspace'

const props = defineProps({ taskId: { type: String, required: true } })

const createMode = computed({
  get: () => etlTaskService.findCreateMode(props.taskId),
  set: (v) => EtlTaskTmp.update({ where: props.taskId, data: { create_mode: v } }),
})
</script>

<template>
  <div>
    <label class="label-field" for="create-mode">{{ $t('createMode') }} </label>
    <VTooltip location="top" transition="slide-y-transition">
      <template #activator="{ props }">
        <VIcon
          class="ml-1 cursor--pointer"
          size="14"
          color="primary"
          icon="mxs:questionCircle"
          v-bind="props"
        />
      </template>
      <span>{{ $t('modesForCreatingTbl') }}</span>
      <table>
        <tr v-for="(v, key) in ETL_CREATE_MODES" :key="`${key}`">
          <td>{{ v }}:</td>
          <td class="font-weight-bold">{{ $t(`info.etlCreateMode.${v}`) }}</td>
        </tr>
      </table>
    </VTooltip>
    <VSelect
      id="create-mode"
      v-model="createMode"
      :items="Object.values(ETL_CREATE_MODES)"
      item-title="text"
      item-value="id"
      class="mb-2"
      density="compact"
      hide-details
    />
  </div>
</template>
