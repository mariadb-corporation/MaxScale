<template>
    <div
        v-if="'type' in item || 'description' in item || 'unit' in item || 'default_value' in item"
        :id="`param-${item.id}_${componentId}`"
        class="pointer"
        :class="[isTree ? 'override-td--padding--tree' : 'override-td--padding']"
    >
        {{ item.id }}
    </div>
    <span v-else>
        {{ item.id }}
    </span>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
PROPS:
- componentId: an unique id needed it to make v-tooltip activator works
- item: parameter input object
*/
export default {
    name: 'parameter-tooltip-activator',
    props: {
        item: { type: Object, required: true }, // always require to have "id" attribute
        componentId: { type: String, required: true },
        isTree: { type: Boolean, default: false },
    },
}
</script>
