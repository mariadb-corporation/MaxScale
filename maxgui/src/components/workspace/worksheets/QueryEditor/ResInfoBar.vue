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
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer.vue'
import IncompleteIndicator from '@wkeComps/QueryEditor/IncompleteIndicator.vue'
import { MAX_RENDERED_COLUMNS } from '@/constants/workspace'

const props = defineProps({
  result: { type: Object, required: true },
  height: { type: Number, default: 28 },
  width: { type: Number, default: 0 },
  isLoading: { type: Boolean, default: false },
  startTime: { type: Number, default: 0 },
  execTime: { type: Number, default: -1 },
  endTime: { type: Number, default: 0 },
})

const typy = useTypy()
const showColLimitInfo = ref(false)

const fields = computed(() => typy(props.result, 'fields').safeArray)
watch(
  fields,
  (v) => {
    if (v.length > MAX_RENDERED_COLUMNS) showColLimitInfo.value = true
  },
  { immediate: true }
)
</script>
<template>
  <VSheet
    :height="height"
    class="w-100 d-inline-flex align-center border-top--separator bg-light-gray info-bar text-text"
  >
    <GblTooltipActivator
      v-if="$typy(result, 'statement').isDefined && !isLoading"
      :data="{ txt: result.statement.text, interactive: true }"
      class="mr-2 cursor--pointer"
      data-test="exec-sql"
      :maxWidth="width / 2.5"
    >
      {{ result.statement.text }}
    </GblTooltipActivator>
    <VSpacer />
    <IncompleteIndicator v-if="!isLoading" class="mr-2" :result="result" />
    <VTooltip v-if="showColLimitInfo" location="top" max-width="400">
      <template #activator="{ props }">
        <span
          data-test="column-limit-info"
          class="mr-2 cursor--pointer text-truncate d-flex align-center text-warning"
          v-bind="props"
        >
          <VIcon size="14" color="warning" class="mr-2" icon="mxs:alertWarning" />
          {{ $t('columnsLimit') }}
        </span>
      </template>
      {{ $t('info.columnsLimit') }}
    </VTooltip>
    <DurationTimer v-if="startTime" :start="startTime" :execTime="execTime" :end="endTime" />
  </VSheet>
</template>

<style lang="scss" scoped>
.info-bar {
  font-size: 0.75rem;
}
</style>
