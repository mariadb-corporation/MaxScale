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
        :max-width="600"
        :max-height="600"
        allow-overflow
    >
        <v-sheet
            class="mxs-truncate-tooltip-menu py-2 px-4 text-body-2"
            :class="[$typy(truncate_tooltip_item, 'contentClass').safeString]"
        >
            <span :style="{ wordBreak: 'break-word' }">{{ truncate_tooltip_item.txt }}</span>
        </v-sheet>
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
 * Change Date: 2027-07-24
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
    },
}
</script>

<style lang="scss">
.mxs-truncate-tooltip-menu {
    background: #303033 !important;
    opacity: 0.9;
    color: #d9dae0 !important;
}
</style>
