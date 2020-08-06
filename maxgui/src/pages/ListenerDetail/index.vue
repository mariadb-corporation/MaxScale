<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(currentListener)" class="px-6">
            <page-header :currentListener="currentListener" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-collapse
                        :searchKeyWord="searchKeyWord"
                        :resourceId="currentListener.id"
                        :parameters="currentListener.attributes.parameters"
                        :moduleParameters="processedModuleParameters"
                        :loading="
                            loadingModuleParams ? true : overlay === OVERLAY_TRANSPARENT_LOADING
                        "
                        :editable="false"
                    />
                </v-col>
                <services-table
                    :searchKeyWord="searchKeyWord"
                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                    :serviceTableRow="serviceTableRow"
                />
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
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapGetters, mapActions } from 'vuex'
import PageHeader from './PageHeader'
import ServicesTable from './ServicesTable'

export default {
    components: {
        PageHeader,

        ServicesTable,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            serviceTableRow: [],
            processedModuleParameters: [],
            loadingModuleParams: true,
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            searchKeyWord: 'searchKeyWord',
            moduleParameters: 'moduleParameters',
            currentListener: 'listener/currentListener',
        }),
    },

    async created() {
        await this.fetchListenerById(this.$route.params.id)
        /*  wait until get currentListener to fetch service state
            and module parameters
        */
        const {
            attributes: { parameters: { protocol = null } = {} } = {},
            relationships: { services: { data: servicesData = [] } = {} } = {},
        } = this.currentListener

        await this.serviceTableRowProcessing(servicesData)

        if (protocol) await this.fetchModuleParameters(protocol)
        this.loadingModuleParams = true
        await this.processModuleParameters()
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchListenerById: 'listener/fetchListenerById',
        }),

        async processModuleParameters() {
            if (this.moduleParameters.length) {
                this.processedModuleParameters = this.moduleParameters
                const self = this
                await this.$help.delay(150).then(() => (self.loadingModuleParams = false))
            }
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
