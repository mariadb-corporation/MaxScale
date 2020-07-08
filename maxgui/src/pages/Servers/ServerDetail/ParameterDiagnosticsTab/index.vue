<template>
    <v-row>
        <!-- PARAMETERS TABLE -->
        <v-col class="py-0 my-0" cols="6">
            <details-parameters-collapse
                :searchKeyWord="searchKeyWord"
                :resourceId="currentServer.id"
                :parameters="currentServer.attributes.parameters"
                :moduleParameters="parameters"
                usePortOrSocket
                :updateResourceParameters="updateServerParameters"
                :onEditSucceeded="onEditSucceeded"
                :loading="loadingModuleParams ? true : loading"
            />
        </v-col>
        <v-col class="py-0 my-0" cols="6">
            <collapse
                :toggleOnClick="() => (showMonitorDiagnostics = !showMonitorDiagnostics)"
                :isContentVisible="showMonitorDiagnostics"
                :title="`${$t('monitorDiagnostics')}`"
            >
                <template v-slot:content>
                    <data-table
                        :search="searchKeyWord"
                        :headers="variableValueTableHeaders"
                        :data="monitorDiagnosticsTableRow"
                        :loading="loading"
                        tdBorderLeft
                        showAll
                        isTree
                    />
                </template>
            </collapse>
        </v-col>
    </v-row>
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
export default {
    name: 'parameter-diagnostics-tab',

    props: {
        searchKeyWord: { type: String, required: true },
        currentServer: { type: Object, required: true },
        updateServerParameters: { type: Function, required: true },
        onEditSucceeded: { type: Function, required: true },
        loading: { type: Boolean, required: true },
    },

    data() {
        return {
            // parameters
            isValid: false,
            showMonitorDiagnostics: true,
            parameters: [],
            loadingModuleParams: true,
            //MONITOR
            monitorDiagnosticsTableRow: [],
            // COMMOn
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '65%' },
                { text: 'Value', value: 'value', width: '35%', editableCol: true },
            ],
        }
    },

    async created() {
        await Promise.all([this.fetchMonitorDiagnostics(), this.fetchServerParams()])
    },
    methods: {
        async fetchServerParams() {
            let self = this
            let res = await self.axios.get(`/maxscale/modules/servers?fields[module]=parameters`)
            const { attributes: { parameters = [] } = {} } = res.data.data
            self.parameters = parameters
            self.loadingModuleParams = true
            await self.$help.delay(150).then(() => (self.loadingModuleParams = false))
        },
        async fetchMonitorDiagnostics() {
            let self = this
            if (!self.$help.lodash.isEmpty(self.currentServer.relationships.monitors)) {
                const { relationships: { monitors = {} } = {} } = self.currentServer

                let res = await this.axios.get(
                    `/monitors/${monitors.data[0].id}?fields[monitors]=monitor_diagnostics`
                )
                const {
                    attributes: {
                        monitor_diagnostics: { server_info },
                    },
                } = res.data.data

                let monitorDiagnosticsObj = server_info.find(
                    server => server.name === self.currentServer.id
                )
                let level = 0
                const keepPrimitiveValue = false
                this.monitorDiagnosticsTableRow = self.$help.objToArrOfObj(
                    monitorDiagnosticsObj,
                    keepPrimitiveValue,
                    level
                )
            }
        },
    },
}
</script>
