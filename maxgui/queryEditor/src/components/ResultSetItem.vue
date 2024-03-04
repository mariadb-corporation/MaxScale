<template>
    <v-list-item
        class="result-set-item pointer mxs-color-helper"
        :class="{ 'text-error': isErrorTab, 'result-set-item--active': activeId === source.id }"
        @click="onClick(source)"
    >
        {{ source.id }}
    </v-list-item>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
export default {
    name: 'result-set-item',
    props: {
        source: { type: Object, required: true },
        activeId: { type: String, default: '' },
        errorTabId: { type: String, default: '' },
        onClick: { type: Function },
    },
    computed: {
        isErrorTab() {
            return this.source.id === this.errorTabId
        },
    },
}
</script>

<style lang="scss" scoped>
.result-set-item {
    &--active {
        &::before {
            opacity: 0.16;
            background-color: currentColor;
        }
    }
}
</style>
