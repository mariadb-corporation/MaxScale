<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_listener)" class="px-6">
            <page-header :currentListener="current_listener" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-table
                        :resourceId="current_listener.id"
                        :parameters="current_listener.attributes.parameters"
                        :editable="false"
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
 * Change Date: 2025-03-24
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
            current_listener: state => state.listener.current_listener,
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
            fetchListenerById: 'listener/fetchListenerById',
        }),
        async initialFetch() {
            await this.fetchListenerById(this.$route.params.id)
            // wait until get current_listener to fetch service state and module parameters
            const {
                attributes: { parameters: { protocol = null } = {} } = {},
                relationships: { services: { data: servicesData = [] } = {} } = {},
            } = this.current_listener

            await this.serviceTableRowProcessing(servicesData)
            if (protocol) await this.fetchModuleParameters(protocol)
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
                    caller: 'listener-details-serviceTableRowProcessing',
                })
                const { id, type, attributes: { state = null } = {} } = data
                await arr.push({ id: id, state: state, type: type })
            })

            this.serviceTableRow = arr
        },
    },
}
</script>
