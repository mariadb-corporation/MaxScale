<template>
    <page-wrapper>
        <v-sheet class="mt-2">
            <page-header />
            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab" :href="'#' + tab">
                    {{ tab }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5" :value="tabs[0]">
                        <v-col cols="7">
                            <details-parameters-table
                                v-if="maxscale_parameters"
                                resourceId="maxscale"
                                :parameters="maxscale_parameters"
                                :overridingModuleParams="overridingModuleParams"
                                :updateResourceParameters="updateMaxScaleParameters"
                                :onEditSucceeded="fetchMaxScaleParameters"
                                isTree
                            />
                        </v-col>
                    </v-tab-item>

                    <v-tab-item :value="tabs[1]" class="pt-5">
                        <v-col cols="12">
                            <log-container
                                :maxscaleOverviewInfo="maxscale_overview_info"
                                :shouldFetchLogs="shouldFetchLogs"
                            />
                        </v-col>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
import PageHeader from './PageHeader'
import LogContainer from './LogContainer'
export default {
    name: 'settings',
    components: {
        PageHeader,
        LogContainer,
    },
    data() {
        return {
            currentActiveTab: null,
            tabs: [this.$t('maxScaleParameters'), this.$t('maxscaleLogs')],
            overridingModuleParams: [],
            shouldFetchLogs: false,
        }
    },
    computed: {
        ...mapState({
            module_parameters: 'module_parameters',
            maxscale_parameters: state => state.maxscale.maxscale_parameters,
            maxscale_overview_info: state => state.maxscale.maxscale_overview_info,
        }),
    },
    watch: {
        currentActiveTab: async function(val) {
            switch (val) {
                case this.$t('maxScaleParameters'):
                    this.shouldFetchLogs = false
                    await Promise.all([
                        this.fetchMaxScaleParameters(),
                        this.fetchModuleParameters('maxscale'),
                    ])
                    this.processingModuleParams()
                    break
                case this.$t('maxscaleLogs'):
                    this.fetchMaxScaleOverviewInfo()
                    this.shouldFetchLogs = true
            }
        },
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            fetchMaxScaleParameters: 'maxscale/fetchMaxScaleParameters',
            updateMaxScaleParameters: 'maxscale/updateMaxScaleParameters',
            fetchMaxScaleOverviewInfo: 'maxscale/fetchMaxScaleOverviewInfo',
        }),
        processingModuleParams() {
            const parameters = this.$help.lodash.cloneDeep(this.module_parameters)
            // hard code type for child parameter of log_throttling
            const log_throttingIndex = parameters.findIndex(
                param => param.name === 'log_throttling'
            )
            const log_throttling = parameters[log_throttingIndex]

            const log_throttling_child_params = [
                {
                    name: 'count',
                    type: 'count',
                    modifiable: true,
                    default_value: log_throttling.default_value.count,
                    description: 'Positive integer specifying the number of logged times',
                },
                {
                    name: 'suppress',
                    type: 'duration',
                    modifiable: true,
                    unit: 'ms',
                    default_value: log_throttling.default_value.suppress,
                    description: 'The suppressed duration before the logging of a particular error',
                },
                {
                    name: 'window',
                    type: 'duration',
                    modifiable: true,
                    unit: 'ms',
                    default_value: log_throttling.default_value.window,
                    description: 'The duration that a particular error may be logged',
                },
            ]

            const left = parameters.slice(0, log_throttingIndex + 1)
            const right = parameters.slice(log_throttingIndex + 1)

            this.overridingModuleParams = [...left, ...log_throttling_child_params, ...right]
        },
    },
}
</script>
