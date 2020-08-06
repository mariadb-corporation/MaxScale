<template>
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
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            currentMonitorDiagnostics: 'monitor/currentMonitorDiagnostics',
        }),

        /**
         * This function fetching monitor diagonostics based on monitor id
         */
        monitorDiagnosticsTableRow: function() {
            let tableRow = []
            if (!this.$help.lodash.isEmpty(this.currentMonitorDiagnostics)) {
                const {
                    attributes: {
                        monitor_diagnostics: { server_info = [] },
                    },
                } = this.currentMonitorDiagnostics
                const self = this
                let monitorDiagnosticsObj = server_info.find(
                    server => server.name === self.$route.params.id
                )
                let level = 0
                const keepPrimitiveValue = false
                tableRow = self.$help.objToArrOfObj(
                    monitorDiagnosticsObj,
                    keepPrimitiveValue,
                    level
                )
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
