<template>
    <v-icon v-if="icon" :color="icon.color" :size="icon.size">
        {{ icon.frame }}
    </v-icon>
</template>

<script>
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
import { NODE_TYPES } from '@wsSrc/constants'

export default {
    name: 'mxs-schema-node-icon',
    props: {
        node: { type: Object, required: true },
        size: { type: Number, required: true },
    },
    computed: {
        pk() {
            return { frame: 'mdi-key', color: 'primary' }
        },
        uqKey() {
            return { frame: '$vuetify.icons.mxs_uniqueIndexKey', color: 'accent' }
        },
        indexKey() {
            return { frame: '$vuetify.icons.mxs_indexKey', color: 'accent' }
        },
        sheet() {
            const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPES
            const { type, data = {} } = this.node || {}
            switch (type) {
                case SCHEMA:
                    return { frame: 'mdi-database-outline' }
                case TBL:
                    return { frame: 'mdi-table' }
                case VIEW:
                    return { frame: 'mdi-table-eye' }
                case SP:
                    return { frame: 'mdi-database-cog-outline' }
                case FN:
                    return { frame: 'mdi-function-variant' }
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
                        return { frame: 'mdi-table-column' }
                    }
                case TRIGGER:
                    return { frame: 'mdi-flash-outline', color: 'warning' }
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
                size: frame.includes('$vuetify.icons.mxs_') ? this.size : this.mdIconSize,
            }
        },
    },
}
</script>
