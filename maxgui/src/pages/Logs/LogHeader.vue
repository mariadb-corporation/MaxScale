<template>
    <div class="d-flex flex-row align-center">
        <span class="color text-field-text d-block mr-2"> log_source: {{ log_source }} </span>
        <v-spacer />
        <filter-list
            v-model="chosenLogLevels"
            returnObject
            :label="$t('filterBy')"
            :items="allLogLevels"
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
                        :class="{ 'filter-list-toggle--active': value }"
                    >
                        arrow_drop_down
                    </v-icon>
                </v-btn>
            </template>
        </filter-list>
    </div>
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
import { mapMutations, mapState } from 'vuex'
export default {
    name: 'log-header',
    computed: {
        ...mapState({
            log_source: state => state.maxscale.log_source,
            chosen_log_levels: state => state.maxscale.chosen_log_levels,
            MAXSCALE_LOG_LEVELS: state => state.app_config.MAXSCALE_LOG_LEVELS,
        }),
        allLogLevels() {
            return this.strToObj(this.MAXSCALE_LOG_LEVELS)
        },
        /**
         * Chosen_log_levels is an array of strings but chosenLogLevels has
         * to be an array of objects with `text` as a property
         */
        chosenLogLevels: {
            get() {
                return this.strToObj(this.chosen_log_levels)
            },
            set(v) {
                this.SET_CHOSEN_LOG_LEVELS(v.map(item => item.text))
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_CHOSEN_LOG_LEVELS: 'maxscale/SET_CHOSEN_LOG_LEVELS',
        }),
        strToObj: arr => arr.map(str => ({ text: str })),
    },
}
</script>
