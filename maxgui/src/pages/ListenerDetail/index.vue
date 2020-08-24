<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_listener)" class="px-6">
            <page-header :currentListener="current_listener" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-collapse
                        :resourceId="current_listener.id"
                        :parameters="current_listener.attributes.parameters"
                        :editable="false"
                    />
                </v-col>
                <v-col cols="6">
                    <relationship-table
                        relationshipType="services"
                        :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
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
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapActions, mapState } from 'vuex'
import PageHeader from './PageHeader'

export default {
    components: {
        PageHeader,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            serviceTableRow: [],
        }
    },
    computed: {
        ...mapState({
            overlay_type: 'overlay_type',
            current_listener: state => state.listener.current_listener,
        }),
    },

    async created() {
        await this.fetchListenerById(this.$route.params.id)
        /*  wait until get current_listener to fetch service state
            and module parameters
        */
        const {
            attributes: { parameters: { protocol = null } = {} } = {},
            relationships: { services: { data: servicesData = [] } = {} } = {},
        } = this.current_listener

        await this.serviceTableRowProcessing(servicesData)
        if (protocol) await this.fetchModuleParameters(protocol)
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchListenerById: 'listener/fetchListenerById',
        }),

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
