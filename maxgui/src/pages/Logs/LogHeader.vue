<template>
    <div class="d-flex flex-row align-center">
        <span class="mxs-color-helper text-grayed-out d-block mr-2">
            log_source: {{ log_source }}
        </span>
        <v-spacer />
        <mxs-filter-list
            v-model="hiddenLogLevels"
            :label="$mxs_t('filterBy')"
            :items="allLogLevels"
            :maxHeight="400"
        >
            <template v-slot:activator="{ data: { on, attrs, value, label } }">
                <v-btn
                    small
                    class="text-capitalize font-weight-medium"
                    outlined
                    depressed
                    color="primary"
                    v-bind="attrs"
                    v-on="on"
                >
                    <v-icon size="16" color="primary" class="mr-1">
                        $vuetify.icons.mxs_filter
                    </v-icon>
                    {{ label }}
                    <v-icon
                        size="24"
                        color="primary"
                        :class="[value ? 'rotate-up' : 'rotate-down']"
                    >
                        mdi-menu-down
                    </v-icon>
                </v-btn>
            </template>
        </mxs-filter-list>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
            hidden_log_levels: state => state.maxscale.hidden_log_levels,
            MAXSCALE_LOG_LEVELS: state => state.app_config.MAXSCALE_LOG_LEVELS,
        }),
        allLogLevels() {
            return this.MAXSCALE_LOG_LEVELS
        },
        hiddenLogLevels: {
            get() {
                return this.hidden_log_levels
            },
            set(v) {
                this.SET_HIDDEN_LOG_LEVELS(v)
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_HIDDEN_LOG_LEVELS: 'maxscale/SET_HIDDEN_LOG_LEVELS',
        }),
    },
}
</script>
