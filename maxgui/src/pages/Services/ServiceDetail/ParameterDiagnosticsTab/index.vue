<template>
    <v-row>
        <!-- PARAMETERS TABLE -->
        <v-col class="py-0 my-0" cols="6">
            <details-parameters-collapse
                v-if="currentService.attributes.parameters && moduleParameters.length"
                :searchKeyWord="searchKeyWord"
                :resourceId="currentService.id"
                :parameters="currentService.attributes.parameters"
                :moduleParameters="moduleParameters"
                :requiredParams="['user', 'password']"
                :updateResourceParameters="updateServiceParameters"
                :onEditSucceeded="onEditSucceeded"
                :loading="loadingModuleParams ? true : loading"
            />
        </v-col>

        <v-col class="py-0 my-0" cols="6">
            <collapse
                :toggleOnClick="() => (showRouterDiagnostics = !showRouterDiagnostics)"
                :isContentVisible="showRouterDiagnostics"
                :title="`${$t('routerDiagnostics')}`"
            >
                <template v-slot:content>
                    <data-table
                        :search="searchKeyWord"
                        :headers="variableValueTableHeaders"
                        :data="routerDiagnosticsTableRow"
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
        currentService: { type: Object, required: true },
        updateServiceParameters: { type: Function, required: true },
        onEditSucceeded: { type: Function, required: true },
        loading: { type: Boolean, required: true },
    },

    data() {
        return {
            // diagnostics table
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '65%' },
                { text: 'Value', value: 'value', width: '35%', editableCol: true },
            ],
            showRouterDiagnostics: true,
            // for parameters
            moduleParameters: [],
            loadingModuleParams: true,
        }
    },

    computed: {
        routerDiagnosticsTableRow: function() {
            let currentService = this.currentService
            if (!this.$help.lodash.isEmpty(currentService)) {
                const { attributes: { router_diagnostics = {} } = {} } = currentService

                const keepPrimitiveValue = true
                let level = 0
                let tableRow = this.$help.objToArrOfObj(
                    router_diagnostics,
                    keepPrimitiveValue,
                    level
                )
                return tableRow
            }
            return []
        },
    },

    async created() {
        let self = this
        let res = await self.axios.get(
            `/maxscale/modules/${self.currentService.attributes.router}?fields[module]=parameters`
        )
        const { attributes: { parameters = [] } = {} } = res.data.data
        self.moduleParameters = parameters

        self.loadingModuleParams = true
        await self.$help.delay(150).then(() => (self.loadingModuleParams = false))
    },
}
</script>
