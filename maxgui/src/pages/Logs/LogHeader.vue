<template>
    <div class="log-header d-flex flex-row align-center">
        <span class="mxs-color-helper text-grayed-out d-flex mr-2 align-self-end">
            {{ $mxs_t('logSource') }}: {{ log_source }}
        </span>
        <v-spacer />
        <mxs-filter-list
            v-model="ignoreLogLevels"
            :items="MAXSCALE_LOG_LEVELS"
            activatorClass="mr-2"
            :label="$mxs_t('priorities')"
        />
        <date-range-picker v-model="dateRange" :height="28" />
        <v-btn
            small
            class="ml-2 text-capitalize font-weight-medium"
            outlined
            depressed
            color="primary"
            @click="applyFilter"
        >
            {{ $mxs_t('filter') }}
        </v-btn>
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
    data() {
        return {
            ignoreLogLevels: [],
            dateRange: [],
        }
    },
    computed: {
        ...mapState({
            MAXSCALE_LOG_LEVELS: state => state.app_config.MAXSCALE_LOG_LEVELS,
            log_source: state => state.maxscale.log_source,
        }),
    },
    methods: {
        ...mapMutations({ SET_LOG_FILTER: 'maxscale/SET_LOG_FILTER' }),
        applyFilter() {
            this.SET_LOG_FILTER({
                priorities: this.MAXSCALE_LOG_LEVELS.filter(
                    type => !this.ignoreLogLevels.includes(type)
                ),
                date_range: this.dateRange,
            })
        },
    },
}
</script>
