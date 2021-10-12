<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_filter)" class="px-6">
            <page-header :currentFilter="current_filter" />
            <v-row class="my-0">
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-table
                        :resourceId="current_filter.id"
                        :parameters="current_filter.attributes.parameters"
                        :updateResourceParameters="updateFilterParameters"
                        :onEditSucceeded="dispatchFetchFilter"
                    />
                </v-col>
                <v-col cols="6">
                    <relationship-table
                        relationshipType="services"
                        :tableRows="serviceTableRow"
                        readOnly
                        :addable="false"
                    />
                </v-col>
            </v-row>
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
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
import PageHeader from './PageHeader'

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
        }),
    },
    watch: {
        // re-fetch when the route changes
        $route: async function() {
            await this.initialFetch()
        },
    },
    async created() {
        await this.initialFetch()
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
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
            servicesData.forEach(async service => {
                const data = await this.getResourceState({
                    resourceId: service.id,
                    resourceType: 'services',
                    caller: 'filter-details-serviceTableRowProcessing',
                })
                const { id, type, attributes: { state = null } = {} } = data
                await arr.push({ id: id, state: state, type: type })
            })

            this.serviceTableRow = arr
        },
    },
}
</script>
