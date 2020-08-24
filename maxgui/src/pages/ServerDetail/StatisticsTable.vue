<template>
    <!-- STATISTICS TABLE -->
    <v-col cols="12" class="pa-0 ma-0">
        <collapse
            :toggleOnClick="() => (showStatistics = !showStatistics)"
            :isContentVisible="showStatistics"
            :title="`${$tc('statistics', 2)}`"
        >
            <template v-slot:content>
                <data-table
                    :search="search_keyword"
                    :headers="variableValueTableHeaders"
                    :data="statisticsTableRow"
                    tdBorderLeft
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
    name: 'statistics-table',

    props: {
        loading: { type: Boolean, required: true },
    },
    data() {
        return {
            //statistics
            variableValueTableHeaders: [
                { text: 'Variable', value: 'id', width: '65%' },
                { text: 'Value', value: 'value', width: '35%' },
            ],
            showStatistics: true,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            current_server: state => state.server.current_server,
        }),

        statisticsTableRow: function() {
            const currentServerClone = this.$help.lodash.cloneDeep(this.current_server)
            // Set fallback null value if properties doesnt exist
            const { attributes: { statistics = null } = {} } = currentServerClone
            return this.$help.objToArrOfNodes({
                obj: statistics,
                keepPrimitiveValue: false,
                level: 0,
            })
        },
    },
}
</script>
