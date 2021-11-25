<template>
    <filter-list
        v-model="chosenLogLevels"
        returnObject
        :label="$t('filterBy')"
        :cols="allLogLevels"
        :maxHeight="400"
    >
        <template v-slot:activator="{ data: { on, attrs, value, label } }">
            <v-btn
                small
                class="text-capitalize font-weight-medium"
                outlined
                depressed
                color="accent-dark"
                v-bind="attrs"
                v-on="on"
            >
                <v-icon size="16" color="accent-dark" class="mr-1">
                    $vuetify.icons.filter
                </v-icon>
                {{ label }}
                <v-icon
                    size="24"
                    color="accent-dark"
                    :class="{ 'column-list-toggle--active': value }"
                >
                    arrow_drop_down
                </v-icon>
            </v-btn>
        </template>
    </filter-list>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'log-filter',
    props: {
        value: { type: Array, required: true },
    },
    data() {
        return {
            allLogLevels: [
                { text: 'alert' },
                { text: 'error' },
                { text: 'warning' },
                { text: 'notice' },
                { text: 'info' },
                { text: 'debug' },
            ],
        }
    },
    computed: {
        chosenLogLevels: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
    },
    mounted() {
        this.chosenLogLevels = this.allLogLevels
    },
}
</script>
