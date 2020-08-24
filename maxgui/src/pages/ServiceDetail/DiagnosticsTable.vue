<template>
    <v-col class="py-0 my-0" cols="6">
        <collapse
            :toggleOnClick="() => (showRouterDiagnostics = !showRouterDiagnostics)"
            :isContentVisible="showRouterDiagnostics"
            :title="`${$t('routerDiagnostics')}`"
        >
            <template v-slot:content>
                <data-table
                    :search="search_keyword"
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
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
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
        ...mapState({
            search_keyword: 'search_keyword',
            current_service: state => state.service.current_service,
        }),

        routerDiagnosticsTableRow: function() {
            const { attributes: { router_diagnostics = {} } = {} } = this.current_service
            let tableRow = this.$help.objToArrOfNodes({
                obj: router_diagnostics,
                keepPrimitiveValue: true,
                level: 0,
            })
            return tableRow
        },
    },
}
</script>
