<template>
    <span
        :id="tooltipItem.activatorID || componentActivatorID"
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
 * Change Date: 2028-04-03
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
        debounce: { type: Number, default: 0 },
    },
    computed: {
        style() {
            if (this.maxWidth) return { maxWidth: `${this.maxWidth}px` }
            return { maxWidth: '100%' } // clientWidth
        },
        componentActivatorID() {
            return `mxs-truncate-str__${this.$helpers.uuidv1()}`
        },
        truncateTooltipItem() {
            if (this.tooltipItem.activatorID) return this.tooltipItem
            return {
                ...this.tooltipItem,
                activatorID: this.componentActivatorID,
            }
        },
    },
    created() {
        this.debouncedMouseEnter = this.$helpers.lodash.debounce(() => {
            this.SET_TRUNCATE_TOOLTIP_ITEM(
                this.$typy(this.$refs, 'string.scrollWidth').safeNumber >
                    this.$typy(this.$refs, 'string.clientWidth').safeNumber
                    ? this.truncateTooltipItem
                    : null
            )
        }, this.debounce)
    },
    methods: {
        ...mapMutations({ SET_TRUNCATE_TOOLTIP_ITEM: 'mxsApp/SET_TRUNCATE_TOOLTIP_ITEM' }),
        mouseenter() {
            this.debouncedMouseEnter()
        },
    },
}
</script>
