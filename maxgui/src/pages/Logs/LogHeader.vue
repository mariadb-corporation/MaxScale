<template>
    <div class="d-flex flex-row align-center">
        <span class="mxs-color-helper text-grayed-out d-flex mr-2 align-self-end">
            {{ $mxs_t('logSource') }}: {{ log_source }}
        </span>
        <v-spacer />
        <div class="mr-2">
            <label class="field__label mxs-color-helper text-small-text d-block">
                {{ $mxs_t('timeFrame') }}
            </label>
            <date-range-picker v-model="dateRange" :height="28" />
        </div>
        <mxs-filter-list v-model="hiddenLogLevels" :items="allLogLevels" :maxHeight="400">
            <template v-slot:activator="{ data: { on, attrs } }">
                <div v-bind="attrs" :style="{ width: '220px' }" v-on="on">
                    <label class="field__label mxs-color-helper text-small-text d-block">
                        {{ $mxs_t('logLevels') }}
                    </label>
                    <v-select
                        :value="getChosenLogLevels"
                        :items="MAXSCALE_LOG_LEVELS"
                        outlined
                        class="vuetify-input--override v-select--mariadb error--text__bottom"
                        :menu-props="{
                            contentClass: 'v-select--menu-mariadb',
                            contentClass: 'v-select--menu-mariadb',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="28"
                        attach
                        hide-details
                        readonly
                        multiple
                        :placeholder="$mxs_t('selectLogLevels')"
                    >
                        <template v-slot:prepend-inner>
                            <v-icon size="16" class="mr-1 color--inherit">
                                $vuetify.icons.mxs_filter
                            </v-icon>
                        </template>
                        <template v-slot:selection="{ item, index }">
                            <template v-if="index === 0">
                                <span class="v-select__selection v-select__selection--comma">
                                    {{ allLogLevelsChosen ? 'All' : item }}
                                </span>
                            </template>
                            <span
                                v-if="index === 1 && !allLogLevelsChosen"
                                class="v-select__selection v-select__selection--comma mxs-color-helper text-caption text-grayed-out "
                            >
                                (+{{ getChosenLogLevels.length - 1 }} {{ $mxs_t('others') }})
                            </span>
                        </template>
                    </v-select>
                </div>
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
import { mapMutations, mapState, mapGetters } from 'vuex'
export default {
    name: 'log-header',
    computed: {
        ...mapState({
            MAXSCALE_LOG_LEVELS: state => state.app_config.MAXSCALE_LOG_LEVELS,
            log_source: state => state.maxscale.log_source,
            hidden_log_levels: state => state.maxscale.hidden_log_levels,
            log_date_range: state => state.maxscale.log_date_range,
        }),
        ...mapGetters({ getChosenLogLevels: 'maxscale/getChosenLogLevels' }),
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
        dateRange: {
            get() {
                return this.log_date_range
            },
            set(v) {
                this.SET_LOG_DATE_RANGE(v)
            },
        },
        allLogLevelsChosen() {
            return this.getChosenLogLevels.length === this.MAXSCALE_LOG_LEVELS.length
        },
    },
    methods: {
        ...mapMutations({
            SET_HIDDEN_LOG_LEVELS: 'maxscale/SET_HIDDEN_LOG_LEVELS',
            SET_LOG_DATE_RANGE: 'maxscale/SET_LOG_DATE_RANGE',
        }),
    },
}
</script>
