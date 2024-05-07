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
import { NODE_TYPES } from '@/constants/workspace'

const props = defineProps({
  node: { type: Object, required: true },
  size: { type: Number, required: true },
})

const typy = useTypy()
const pk = { value: '$mdiKey', semanticColor: 'primary' }
const uqKey = { value: 'mxs:uniqueIndexKey', semanticColor: 'accent' }
const indexKey = { value: 'mxs:indexKey', semanticColor: 'accent' }
const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPES

const sheet = computed(() => {
  const { type, data = {} } = props.node || {}
  switch (type) {
    case SCHEMA:
      return { value: '$mdiDatabaseOutline' }
    case TBL:
      return { value: '$mdiTable' }
    case VIEW:
      return { value: '$mdiTableEye' }
    case SP:
      return { value: '$mdiDatabaseCogOutline' }
    case FN:
      return { value: '$mdiFunctionVariant' }
    case COL:
    case IDX:
      if (data.COLUMN_KEY === 'PRI' || data.INDEX_NAME === 'PRIMARY') return pk
      else if (
        data.COLUMN_KEY === 'UNI' ||
        (typy(data, 'NON_UNIQUE').isDefined && !data.NON_UNIQUE)
      )
        return uqKey
      else {
        if (data.COLUMN_KEY === 'MUL' || type === IDX) return indexKey
        return { value: '$mdiTableColumn' }
      }
    case TRIGGER:
      return { value: '$mdiFlashOutline', semanticColor: 'warning' }
    default:
      return null
  }
})

/**
 * Material Design icons are font icons, and they don't occupy all available spaces. Therefore,
 * the size must be increased by 2
 */
const mdIconSize = computed(() => props.size + 2)

const icon = computed(() => {
  if (!sheet.value) return null
  const { value, semanticColor = 'blue-azure' } = sheet.value
  return {
    value,
    semanticColor,
    size: value.includes('mxs:') ? props.size : mdIconSize.value,
  }
})
</script>

<template>
  <VIcon v-if="icon" :color="icon.semanticColor" :size="icon.size" :icon="icon.value" />
</template>
