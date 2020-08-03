<template>
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
import { mapGetters } from 'vuex'
export default {
    name: 'diagnostics-table',

    props: {
        loading: { type: Boolean, required: true },
    },

    data() {
        return {
            // diagnostics table
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '65%' },
                { text: 'Value', value: 'value', width: '35%' },
            ],
            showRouterDiagnostics: true,
        }
    },

    computed: {
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            currentService: 'service/currentService',
        }),
        routerDiagnosticsTableRow: function() {
            let currentService = this.currentService
            const { attributes: { router_diagnostics = {} } = {} } = currentService
            const keepPrimitiveValue = true
            let level = 0
            let tableRow = []
            tableRow = this.$help.objToArrOfObj(router_diagnostics, keepPrimitiveValue, level)
            return tableRow
        },
    },
}
</script>
