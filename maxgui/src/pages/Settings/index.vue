<template>
    <page-wrapper>
        <v-sheet class="mt-2">
            <page-header />
            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5">
                        <v-col cols="7">
                            <details-parameters-collapse
                                v-if="maxScaleParameters"
                                :searchKeyword="search_keyword"
                                resourceId="maxscale"
                                :parameters="maxScaleParameters"
                                :moduleParameters="processedModuleParameters"
                                :updateResourceParameters="updateMaxScaleParameters"
                                :onEditSucceeded="fetchMaxScaleParameters"
                                :loading="
                                    loadingModuleParams
                                        ? true
                                        : overlay_type === OVERLAY_TRANSPARENT_LOADING
                                "
                                isTree
                            />
                        </v-col>
                    </v-tab-item>

                    <!-- <v-tab-item class="pt-5">
                        Empty
                    </v-tab-item> -->
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
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapGetters, mapActions, mapState } from 'vuex'
import PageHeader from './PageHeader'

export default {
    name: 'settings',
    components: {
        PageHeader,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            currentActiveTab: null,
            tabs: [
                { name: this.$t('maxScaleParameters') },
                // { name: this.$t('usersAndPermissions') },
            ],
            processedModuleParameters: [],
            loadingModuleParams: true,
        }
    },
    computed: {
        ...mapState({
            overlay_type: 'overlay_type',
            search_keyword: 'search_keyword',
            module_parameters: 'module_parameters',
        }),
        ...mapGetters({
            maxScaleParameters: 'maxscale/maxScaleParameters',
        }),
    },
    async created() {
        await Promise.all([this.fetchMaxScaleParameters(), this.fetchModuleParameters('maxscale')])
        this.loadingModuleParams = true
        await this.processModuleParameters()
    },
    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            fetchMaxScaleParameters: 'maxscale/fetchMaxScaleParameters',
            updateMaxScaleParameters: 'maxscale/updateMaxScaleParameters',
        }),
        async processModuleParameters() {
            if (this.module_parameters.length) {
                const parameters = this.module_parameters
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
                        name: 'window',
                        type: 'duration',
                        modifiable: true,
                        unit: 'ms',
                        default_value: log_throttling.default_value.window,
                        description: 'The duration that a particular error may be logged',
                    },
                    {
                        name: 'suppress',
                        type: 'duration',
                        modifiable: true,
                        unit: 'ms',
                        default_value: log_throttling.default_value.suppress,
                        description:
                            'The suppressed duration before the logging of a particular error',
                    },
                ]

                const left = parameters.slice(0, log_throttingIndex + 1)
                const right = parameters.slice(log_throttingIndex + 1)

                this.processedModuleParameters = [...left, ...log_throttling_child_params, ...right]

                const self = this
                await this.$help.delay(150).then(() => (self.loadingModuleParams = false))
            }
        },
    },
}
</script>
