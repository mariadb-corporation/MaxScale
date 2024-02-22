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
import statusIconHelpers, { ICON_SHEETS } from '@/utils/statusIconHelpers'
const props = defineProps({
  value: { type: String, required: true },
  type: { type: String, required: true },
  size: [Number, String],
})
const typy = useTypy()

const iconSheet = computed(() => typy(ICON_SHEETS, `[${props.type}]`).safeObjectOrEmpty)

const icon = computed(() => {
  const { type, value = '' } = props
  const frameIdx = statusIconHelpers[type](value)
  const { frames = [], colorClasses = [] } = iconSheet.value
  return {
    frame: frames[frameIdx],
    colorClass: colorClasses[frameIdx] || '',
  }
})
</script>

<template>
  <VIcon :class="icon.colorClass" :size="size" :icon="$typy(icon, 'frame').safeString" />
</template>
