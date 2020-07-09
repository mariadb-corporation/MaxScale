<template>
    <v-tooltip
        right
        transition="slide-x-transition"
        content-class="shadow-drop color text-navigation"
        :activator="activator"
        max-width="300"
    >
        <v-sheet style="border-radius: 10px;overflow:auto;" class="pa-4" max-width="300">
            <template v-for="(value, name) in parameterTooltip.item">
                <span v-if="name !== 'id'" :key="name" class="d-block body-2">
                    <span class="mr-1 font-weight-medium text-capitalize"> {{ $t(name) }}: </span>
                    <span> {{ value }}</span>
                </span>
            </template>
        </v-sheet>
    </v-tooltip>
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
- parameterTooltip: Object info of a parameter
- activator: parameter input object
*/
export default {
    name: 'parameter-tooltip',
    props: {
        parameterTooltip: { type: Object, required: true },
        activator: { type: String, required: true },
    },
}
</script>
