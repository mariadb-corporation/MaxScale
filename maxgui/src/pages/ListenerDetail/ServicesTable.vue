<template>
    <v-col cols="6">
        <collapse
            :toggleOnClick="() => (showServices = !showServices)"
            :isContentVisible="showServices"
            :title="`${$tc('services', 2)}`"
            :titleInfo="serviceTableRow.length"
        >
            <template v-slot:content>
                <data-table
                    :headers="serviceTableHeader"
                    :data="serviceTableRow"
                    :noDataText="$t('noEntity', { entityName: $tc('services', 2) })"
                    sortBy="id"
                    :loading="loading"
                    :search="searchKeyWord"
                >
                    <template v-slot:id="{ data: { item: { id } } }">
                        <router-link
                            :key="id"
                            :to="`/dashboard/services/${id}`"
                            class="no-underline"
                        >
                            <span> {{ id }} </span>
                        </router-link>
                    </template>
                    <template v-slot:state="{ data: { item: { state } } }">
                        <icon-sprite-sheet
                            size="13"
                            class="status-icon"
                            :frame="$help.serviceStateIcon(state)"
                        >
                            status
                        </icon-sprite-sheet>
                    </template>
                </data-table>
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

export default {
    name: 'services-table',
    props: {
        searchKeyWord: { type: String, required: true },
        loading: { type: Boolean, required: true },
        serviceTableRow: { type: Array, required: true },
    },
    data() {
        return {
            // services
            showServices: true,
            serviceTableHeader: [
                { text: 'Service', value: 'id' },
                { text: 'Status', value: 'state', align: 'center' },
                { text: '', value: 'action', sortable: false },
            ],
        }
    },
}
</script>
