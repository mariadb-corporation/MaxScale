<template>
    <div class="filters-wrapper">
        <div class="float-right">
            <v-select
                v-model="chosenLogLevels"
                multiple
                :items="allLogLevels"
                outlined
                dense
                :height="36"
                class="std mariadb-select-input"
                :menu-props="{ contentClass: 'mariadb-select-v-menu', bottom: true, offsetY: true }"
                placeholder="Filter by"
                clearable
                @change="updateValue"
            >
                <template v-slot:selection="{ item, index }">
                    <span v-if="index === 0" class="v-select__selection v-select__selection--comma">
                        {{ item }}
                    </span>
                    <span
                        v-if="index === 1"
                        class="v-select__selection v-select__selection--comma color text-caption text-field-text "
                    >
                        (+{{ chosenLogLevels.length - 1 }} {{ $t('others') }})
                    </span>
                </template>
            </v-select>
        </div>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'log-filter',

    data() {
        return {
            chosenLogLevels: [],
            allLogLevels: ['alert', 'error', 'warning', 'notice', 'info', 'debug'],
        }
    },

    methods: {
        updateValue: function(value) {
            this.$emit('get-chosen-log-levels', value)
        },
    },
}
</script>
<style lang="scss" scoped>
.filters-wrapper {
    height: 50px;
}
</style>
