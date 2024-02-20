<script>
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
import { NODE_TYPES } from '@/constants/workspace'

export default {
  props: {
    node: { type: Object, required: true },
    size: { type: Number, required: true },
  },
  computed: {
    pk() {
      return { frame: 'mdiKey', color: 'primary' }
    },
    uqKey() {
      return { frame: 'mxs:uniqueIndexKey', color: 'accent' }
    },
    indexKey() {
      return { frame: 'mxs:indexKey', color: 'accent' }
    },
    sheet() {
      const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPES
      const { type, data = {} } = this.node || {}
      switch (type) {
        case SCHEMA:
          return { frame: 'mdiDatabaseOutline' }
        case TBL:
          return { frame: 'mdiTable' }
        case VIEW:
          return { frame: 'mdiTableEye' }
        case SP:
          return { frame: 'mdiDatabaseCogOutline' }
        case FN:
          return { frame: 'mdiFunctionVariant' }
        case COL:
        case IDX:
          if (data.COLUMN_KEY === 'PRI' || data.INDEX_NAME === 'PRIMARY') return this.pk
          else if (
            data.COLUMN_KEY === 'UNI' ||
            (this.$typy(data, 'NON_UNIQUE').isDefined && !data.NON_UNIQUE)
          )
            return this.uqKey
          else {
            if (data.COLUMN_KEY === 'MUL' || type === IDX) return this.indexKey
            return { frame: 'mdiTableColumn' }
          }
        case TRIGGER:
          return { frame: 'mdiFlashOutline', color: 'warning' }
        default:
          return null
      }
    },
    /**
     * Material Design icons are font icons, and they don't occupy all available spaces. Therefore,
     * the size must be increased by 2
     */
    mdIconSize() {
      return this.size + 2
    },
    icon() {
      if (!this.sheet) return null
      const { frame, color = 'blue-azure' } = this.sheet
      return {
        frame,
        color,
        size: frame.includes('mxs:') ? this.size : this.mdIconSize,
      }
    },
  },
}
</script>

<template>
  <VIcon v-if="icon" :color="icon.color" :size="icon.size" :icon="icon.frame" />
</template>
