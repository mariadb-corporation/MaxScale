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
import { NODE_TYPE_MAP } from '@/constants/workspace'

const props = defineProps({
  type: { type: String, required: true },
  size: { type: Number, default: 16 },
  attrs: { type: Object, default: () => ({}) },
})

const typy = useTypy()
const pk = { value: '$mdiKey', semanticColor: 'primary' }
const uqKey = { value: 'mxs:uniqueIndexKey', semanticColor: 'accent' }
const indexKey = { value: 'mxs:indexKey', semanticColor: 'accent' }
const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPE_MAP

const sheet = computed(() => {
  switch (props.type) {
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
    case IDX: {
      const attrs = props.attrs
      if (attrs.COLUMN_KEY === 'PRI' || attrs.INDEX_NAME === 'PRIMARY') return pk
      else if (
        attrs.COLUMN_KEY === 'UNI' ||
        (typy(attrs, 'NON_UNIQUE').isDefined && !attrs.NON_UNIQUE)
      )
        return uqKey
      else {
        if (attrs.COLUMN_KEY === 'MUL' || props.type === IDX) return indexKey
        return { value: '$mdiTableColumn' }
      }
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
