<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="false"
        sortBy="id"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link :key="id" :to="`/dashboard/listeners/${id}`" class="no-underline">
                <span> {{ id }}</span>
            </router-link>
        </template>
        <template v-slot:state="{ data: { item: { state } } }">
            <icon-sprite-sheet
                size="13"
                class="status-icon mr-1"
                :frame="$help.listenerStateIcon(state)"
            >
                status
            </icon-sprite-sheet>
            <span>{{ state }} </span>
        </template>

        <template v-slot:header-append-serviceIds>
            <span class="ml-1 color text-field-text"> ({{ servicesLength }}) </span>
        </template>

        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <template v-for="serviceId in serviceIds">
                <router-link
                    :key="serviceId"
                    :to="`/dashboard/services/${serviceId}`"
                    class="no-underline"
                >
                    <span> {{ serviceId }} </span>
                </router-link>
            </template>
        </template>
    </data-table>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'listeners',

    data() {
        return {
            tableHeaders: [
                { text: 'Listener', value: 'id', cellTruncated: true },
                { text: 'Port', value: 'port' },
                { text: 'Host', value: 'address' },
                { text: 'State', value: 'state' },
                { text: 'Service', value: 'serviceIds', cellTruncated: true },
            ],
            servicesLength: 0,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_listeners: state => state.listener.all_listeners,
        }),

        /**
         * @return {Array} An array of objects
         */
        tableRows: function() {
            let rows = []
            let allServiceIds = []
            this.all_listeners.forEach(listener => {
                const {
                    id,
                    attributes: {
                        state,
                        parameters: { port, address, socket },
                    },
                    relationships: { services: { data: associatedServices = [] } = {} },
                } = listener

                // always has one service
                const serviceIds = associatedServices.length
                    ? associatedServices.map(item => `${item.id}`)
                    : this.$t('noEntity', { entityName: 'services' })

                if (typeof serviceIds !== 'string')
                    allServiceIds = [...allServiceIds, ...serviceIds]

                let row = {
                    id: id,
                    port: port,
                    address: address,
                    state: state,
                    serviceIds: serviceIds,
                }

                if (port === null) row.address = socket

                rows.push(row)
            })
            const uniqueServiceId = new Set(allServiceIds) // get unique service ids
            this.setServicesLength([...uniqueServiceId].length)
            return rows
        },
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
    },
}
</script>
