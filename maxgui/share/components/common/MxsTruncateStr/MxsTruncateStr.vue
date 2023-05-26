<template>
    <span
        :id="tooltipItem.activatorID ? '' : componentActivatorID"
        ref="string"
        class="d-inline-block text-truncate"
        :style="style"
        v-on="disabled ? null : { mouseenter }"
    >
        <slot> {{ $typy(tooltipItem, 'txt').safeString }}</slot>
    </span>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations } from 'vuex'
export default {
    name: 'mxs-truncate-str',
    props: {
        /* must contain text key. If tooltipItem.activatorID is defined, it uses that as an external
         * activator instead of the componentActivatorID.
         */
        tooltipItem: { type: Object, required: true },
        maxWidth: { type: Number, default: 0 }, // if maxWidth isn't provided, it uses clientWidth
        disabled: { type: Boolean, default: false },
    },
    data() {
        return {
            componentActivatorID: `mxs-truncate-str__${this.$helpers.uuidv1()}`,
            scrollWidth: 0,
            clientWidth: 0,
        }
    },
    computed: {
        style() {
            if (this.maxWidth) return { maxWidth: `${this.maxWidth}px` }
            else return { maxWidth: '100%' } // clientWidth
        },
        activatorID() {
            if (this.tooltipItem.activatorID) return this.tooltipItem.activatorID
            else return this.componentActivatorID
        },
        truncateTooltipItem() {
            return { ...this.tooltipItem, activatorID: this.activatorID }
        },
    },
    methods: {
        ...mapMutations({ SET_TRUNCATE_TOOLTIP_ITEM: 'mxsApp/SET_TRUNCATE_TOOLTIP_ITEM' }),
        mouseenter() {
            const isTruncated = this.$refs.string.scrollWidth > this.$refs.string.clientWidth
            this.scrollWidth = this.$refs.string.scrollWidth
            this.clientWidth = this.$refs.string.clientWidth
            this.SET_TRUNCATE_TOOLTIP_ITEM(isTruncated ? this.truncateTooltipItem : null)
        },
    },
}
</script>
