<template>
    <v-menu
        top
        offset-y
        transition="slide-y-transition"
        :close-on-content-click="false"
        open-on-hover
        allow-overflow
        content-class="shadow-drop"
    >
        <template v-slot:activator="{ on }">
            <div class="pointer d-flex override-td--padding fill-height align-center" v-on="on">
                {{ data.total }}
            </div>
        </template>
        <v-sheet class="py-4 px-3 text-body-2">
            <table class="info-table px-1">
                <tr v-for="(value, key) in data" :key="key">
                    <td class="pr-5">{{ key }}</td>
                    <td>{{ value }}</td>
                </tr>
            </table>
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'memory-cell',
    props: {
        data: { type: Object, required: true },
    },
}
</script>
<style lang="scss" scoped>
.info-table {
    td {
        white-space: nowrap;
        height: 24px;
        line-height: 1.5;
    }
}
</style>
