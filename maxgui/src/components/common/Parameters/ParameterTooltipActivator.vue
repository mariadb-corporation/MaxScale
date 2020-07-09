<template>
    <span
        v-if="'type' in item || 'description' in item || 'unit' in item || 'default_value' in item"
        :id="`param-${item.id}_${componentId}`"
        class="pointer"
    >
        {{ item.id }}
    </span>
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
PROPS:
- componentId: a needed it to make v-tooltip activator works
- item: parameter input object
*/
export default {
    name: 'parameter-tooltip-activator',
    props: {
        item: { type: Object, required: true },
        componentId: { type: String, required: true },
    },
}
</script>
