<template>
    <page-wrapper>
        <v-sheet v-if="!$helpers.lodash.isEmpty(current_listener)" class="pl-6">
            <page-header :currentListener="current_listener" class="pb-3" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-table
                        :resourceId="current_listener.id"
                        :parameters="current_listener.attributes.parameters"
                        :updateResourceParameters="updateListenerParameters"
                        :onEditSucceeded="dispatchFetchListener"
                        :objType="MXS_OBJ_TYPES.LISTENERS"
                    />
                </v-col>
                <v-col cols="6">
                    <relationship-table relationshipType="services" :tableRows="serviceTableRow" />
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
 * Change Date: 2027-10-10
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
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
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
            getResourceData: 'getResourceData',
            fetchListenerById: 'listener/fetchListenerById',
            updateListenerParameters: 'listener/updateListenerParameters',
        }),
        async dispatchFetchListener() {
            await this.fetchListenerById(this.$route.params.id)
        },
        async initialFetch() {
            await this.dispatchFetchListener()
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
