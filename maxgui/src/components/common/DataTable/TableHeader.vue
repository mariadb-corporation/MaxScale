<template>
    <thead class="v-data-table-header">
        <tr>
            <th
                v-for="(header, i) in headers"
                :key="i"
                :style="{
                    padding: header.padding,
                    width: header.width,
                }"
                :class="thClasses(header)"
                @click="header.sortable !== false ? $emit('change-sort', header.value) : null"
            >
                <div class="d-inline-flex justify-center align-center">
                    <span v-if="header.text !== 'Action'">{{ header.text }}</span>
                    <slot :name="`header-append-${header.value}`"> </slot>
                    <v-icon
                        v-if="header.sortable !== false"
                        size="14"
                        class="ml-3 v-data-table-header__icon"
                    >
                        $vuetify.icons.arrowDown
                    </v-icon>
                </div>
            </th>
        </tr>
    </thead>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
headers: {
  text: string,
  value: any,
  width?: string,
  sortable?: boolean
  editableCol?: boolean, if true, apply editable style for that column
  autoTruncate?: boolean, auto truncate cell value
  align?: string, "center || left || right",
}

SLOTS available for this component:
- slot :name="`header-append-${header.value}`"

Emits:
- $emit('change-sort', header.value)
*/
export default {
    name: 'table-header',
    props: {
        headers: { type: Array, required: true },
        sortBy: { type: String, required: true },
        sortDesc: { type: Boolean, required: true },
        // For display tree view
        isTree: { type: Boolean, required: true },
        hasValidChild: { type: Boolean, required: true },
    },
    methods: {
        thClasses(header) {
            return [
                'color bg-color-table-border text-small-text border-bottom-none',
                header.align && `text-${header.align}`,
                header.sortable !== false ? 'pointer sortable' : 'not-sortable',
                this.sortDesc ? 'desc' : 'asc',
                header.value === this.sortBy && 'active',
                header.text === 'Action' && 'px-0',
                this.isTree && this.hasValidChild ? 'py-0 px-12' : 'py-0 px-6',
            ]
        },
    },
}
</script>
