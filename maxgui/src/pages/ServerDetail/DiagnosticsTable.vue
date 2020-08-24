<template>
    <collapse
        :toggleOnClick="() => (showMonitorDiagnostics = !showMonitorDiagnostics)"
        :isContentVisible="showMonitorDiagnostics"
        :title="`${$t('monitorDiagnostics')}`"
    >
        <template v-slot:content>
            <data-table
                :search="search_keyword"
                :headers="variableValueTableHeaders"
                :data="monitorDiagnosticsTableRow"
                :loading="loading"
                tdBorderLeft
                showAll
                isTree
            />
        </template>
    </collapse>
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
        fetchMonitorDiagnostics: { type: Function, required: true },
    },

    data() {
        return {
            showMonitorDiagnostics: true,
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '65%' },
                { text: 'Value', value: 'value', width: '35%' },
            ],
        }
    },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            monitor_diagnostics: state => state.monitor.monitor_diagnostics,
        }),

        monitorDiagnosticsTableRow: function() {
            let tableRow = []
            if (!this.$help.lodash.isEmpty(this.monitor_diagnostics)) {
                const {
                    attributes: {
                        monitor_diagnostics: { server_info = [] },
                    },
                } = this.monitor_diagnostics
                let monitorDiagnosticsObj = server_info.find(
                    server => server.name === this.$route.params.id
                )

                tableRow = this.$help.objToArrOfNodes({
                    obj: monitorDiagnosticsObj,
                    keepPrimitiveValue: false,
                    level: 0,
                })
                return tableRow
            }
            return tableRow
        },
    },
    async created() {
        await this.fetchMonitorDiagnostics()
    },
}
</script>
