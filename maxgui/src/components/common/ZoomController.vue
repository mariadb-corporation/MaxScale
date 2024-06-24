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
import { ZOOM_OPTS } from '@/constants'

defineOptions({ inheritAttrs: false })

const props = defineProps({
  zoomRatio: { type: Number, required: true },
  isFitIntoView: { type: Boolean, required: true },
})
const emit = defineEmits(['update:zoomRatio', 'update:isFitIntoView'])

const { t } = useI18n()

const zoomPct = computed({
  get: () => Math.floor(props.zoomRatio * 100),
  set: (v) => emit('update:zoomRatio', v / 100),
})

function handleShowSelection() {
  return `${props.isFitIntoView ? t('fit') : `${zoomPct.value}%`}`
}
</script>

<template>
  <VTooltip location="top">
    <template #activator="{ props }">
      <VSelect
        v-model.number="zoomPct"
        :items="ZOOM_OPTS"
        density="compact"
        hide-details
        :maxlength="3"
        @keypress="$helpers.preventNonNumericalVal($event)"
        v-bind="{ ...props, ...$attrs }"
      >
        <template #prepend-item>
          <VListItem link @click="emit('update:isFitIntoView', true)">
            {{ $t('fit') }}
          </VListItem>
        </template>
        <template #selection> {{ handleShowSelection() }} </template>
        <template #item="{ props }">
          <VListItem v-bind="props">
            <template #title="{ title }"> {{ `${title}%` }} </template>
          </VListItem>
        </template>
      </VSelect>
    </template>
    {{ $t('zoom') }}
  </VTooltip>
</template>
