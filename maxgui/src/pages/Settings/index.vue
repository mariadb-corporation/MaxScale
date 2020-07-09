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
                                v-if="maxScaleParameters && moduleParameters.length"
                                :searchKeyWord="searchKeyWord"
                                resourceId="maxscale"
                                :parameters="maxScaleParameters"
                                :moduleParameters="moduleParameters"
                                :updateResourceParameters="updateMaxScaleParameters"
                                :onEditSucceeded="fetchMaxScaleParameters"
                                :loading="
                                    loadingModuleParams
                                        ? true
                                        : overlay === OVERLAY_TRANSPARENT_LOADING
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapGetters, mapActions } from 'vuex'
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
            moduleParameters: [],
            loadingModuleParams: true,
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            searchKeyWord: 'searchKeyWord',
            maxScaleParameters: 'maxscale/maxScaleParameters',
        }),
    },
    async created() {
        await Promise.all([this.fetchMaxScaleParameters(), this.fetchModuleParameters()])
    },
    methods: {
        ...mapActions('maxscale', ['updateMaxScaleParameters', 'fetchMaxScaleParameters']),

        async fetchModuleParameters() {
            const self = this
            let res = await self.axios.get(`/maxscale/modules/maxscale?fields[module]=parameters`)
            const { attributes: { parameters = [] } = {} } = res.data.data
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
                    description: 'The suppressed duration before the logging of a particular error',
                },
            ]

            const left = parameters.slice(0, log_throttingIndex + 1)
            const right = parameters.slice(log_throttingIndex + 1)

            self.moduleParameters = [...left, ...log_throttling_child_params, ...right]

            self.loadingModuleParams = true
            await self.$help.delay(150).then(() => (self.loadingModuleParams = false))
        },
    },
}
</script>
