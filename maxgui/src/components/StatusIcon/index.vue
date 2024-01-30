<template>
    <v-icon :class="colorClasses" :size="size">
        {{ $typy(icon, 'frame').safeString }}
    </v-icon>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import statusIconHelpers, { ICON_SHEETS } from '@src/utils/statusIconHelpers'

export default {
    name: 'status-icon',
    props: {
        value: { type: String, required: true },
        type: { type: String, required: true },
        size: [Number, String],
    },
    computed: {
        iconSheet() {
            return this.$typy(ICON_SHEETS, `[${this.type}]`).safeObjectOrEmpty
        },
        icon() {
            const { type, value = '' } = this
            const frameIdx = statusIconHelpers[type](value)
            const { frames = [], colorClasses = [] } = this.iconSheet
            return {
                frame: frames[frameIdx],
                colorClass: colorClasses[frameIdx] || '',
            }
        },
        colorClasses() {
            return `mxs-color-helper ${this.icon.colorClass}`
        },
    },
}
</script>
