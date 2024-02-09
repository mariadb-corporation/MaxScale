<template>
    <page-wrapper>
        <v-sheet v-if="!$helpers.lodash.isEmpty(current_filter)" class="pl-6">
            <page-header :currentFilter="current_filter" class="pb-3" />
            <v-row>
                <v-col cols="6">
                    <details-parameters-table
                        :resourceId="current_filter.id"
                        :parameters="current_filter.attributes.parameters"
                        :moduleParameters="module_parameters"
                        :updateResourceParameters="updateFilterParameters"
                        :onEditSucceeded="dispatchFetchFilter"
                        :objType="MXS_OBJ_TYPES.FILTERS"
                    />
                </v-col>
                <v-col cols="6">
                    <v-row>
                        <v-col cols="12">
                            <relationship-table
                                relationshipType="services"
                                :tableRows="serviceTableRow"
                            />
                        </v-col>
                        <v-col v-if="!$typy(filter_diagnostics).isEmptyObject" cols="12">
                            <details-readonly-table
                                :title="`${$mxs_tc('diagnostics', 2)}`"
                                :tableData="filter_diagnostics"
                                isTree
                                expandAll
                            />
                        </v-col>
                    </v-row>
                </v-col>
            </v-row>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
import PageHeader from './PageHeader'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    components: {
        PageHeader,
    },
    data() {
        return {
            serviceTableRow: [],
        }
    },
    computed: {
        ...mapState({
            current_filter: state => state.filter.current_filter,
            module_parameters: 'module_parameters',
        }),
        filter_diagnostics() {
            return this.$typy(this.current_filter, 'attributes.filter_diagnostics')
                .safeObjectOrEmpty
        },
    },
    watch: {
        // re-fetch when the route changes
        $route: async function() {
            await this.initialFetch()
        },
    },
    async created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
        await this.initialFetch()
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceData: 'getResourceData',
            fetchFilterById: 'filter/fetchFilterById',
            updateFilterParameters: 'filter/updateFilterParameters',
        }),
        async dispatchFetchFilter() {
            await this.fetchFilterById(this.$route.params.id)
        },
        async initialFetch() {
            await this.dispatchFetchFilter()
            // wait until get current_filter to fetch service state  and module parameters
            const {
                attributes: { module: filterModule = null } = {},
                relationships: { services: { data: servicesData = [] } = {} } = {},
            } = this.current_filter

            await this.serviceTableRowProcessing(servicesData)
            if (filterModule) await this.fetchModuleParameters(filterModule)
        },
        /**
         * This function loops through services data to get services state based on
         * service id
         * @param {Array} servicesData name of the service
         */
        async serviceTableRowProcessing(servicesData) {
            let arr = []
            for (const service of servicesData) {
                const { id, type, attributes: { state = null } = {} } = await this.getResourceData({
                    id: service.id,
                    type: 'services',
                })
                arr.push({ id: id, state: state, type: type })
            }
            this.serviceTableRow = arr
        },
    },
}
</script>
