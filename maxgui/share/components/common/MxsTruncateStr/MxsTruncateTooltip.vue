<template>
    <v-menu
        v-if="truncate_tooltip_item"
        :key="truncate_tooltip_item.activatorID"
        :value="Boolean(truncate_tooltip_item)"
        open-on-hover
        :close-on-content-click="false"
        :activator="`#${truncate_tooltip_item.activatorID}`"
        :nudge-left="$typy(truncate_tooltip_item, 'nudgeLeft').safeNumber"
        :nudge-top="$typy(truncate_tooltip_item, 'nudgeTop').safeNumber"
        offset-y
        top
        transition="slide-y-transition"
        content-class="shadow-drop rounded-sm"
        :max-width="800"
        :max-height="600"
        allow-overflow
    >
        <div :class="contentClass">{{ truncate_tooltip_item.txt }}</div>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'mxs-truncate-tooltip',
    computed: {
        ...mapState({ truncate_tooltip_item: state => state.mxsApp.truncate_tooltip_item }),
        contentClass() {
            return [
                'mxs-truncate-tooltip-menu py-2 px-4 text-body-2',
                this.$typy(this.truncate_tooltip_item, 'contentClass').safeString,
            ]
        },
    },
}
</script>

<style lang="scss">
.mxs-truncate-tooltip-menu {
    background: $tooltip-background-color;
    opacity: 0.9;
    color: $tooltip-text-color;
    white-space: pre;
    overflow: auto;
}
</style>
