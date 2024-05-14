<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { ETL_STATUS } from '@/constants/workspace'

const props = defineProps({
  icon: { type: [String, Object], required: true },
  spinning: { type: Boolean, default: false },
})
const { RUNNING, CANCELED, ERROR, COMPLETE } = ETL_STATUS
const typy = useTypy()

const data = computed(() => {
  let value, semanticColor
  switch (props.icon) {
    case RUNNING:
      value = 'mxs:loading'
      semanticColor = 'navigation'
      break
    case CANCELED:
      value = 'mxs:critical'
      semanticColor = 'warning'
      break
    case ERROR:
      value = 'mxs:alertError'
      semanticColor = 'error'
      break
    case COMPLETE:
      value = 'mxs:good'
      semanticColor = 'success'
      break
  }
  if (typy(props.icon).isObject) return props.icon
  return { value, semanticColor }
})
</script>

<template>
  <VIcon
    v-if="data.value"
    size="14"
    :color="data.semanticColor"
    class="mr-1"
    :class="{ 'icon--spinning': spinning }"
    :icon="data.value"
  />
</template>

<style lang="scss" scoped>
@keyframes rotating {
  from {
    transform: rotate(0deg);
  }
  to {
    transform: rotate(360deg);
  }
}
.icon--spinning {
  animation: rotating 1.5s linear infinite;
}
</style>
