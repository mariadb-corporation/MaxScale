<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="false"
        sortBy="id"
        :itemsPerPage="-1"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link
                :key="id"
                v-mxs-highlighter="{ keyword: search_keyword, txt: id }"
                :to="`/dashboard/listeners/${id}`"
                class="rsrc-link"
            >
                {{ id }}
            </router-link>
        </template>
        <template v-slot:state="{ data: { item: { state } } }">
            <status-icon
                size="16"
                class="listener-state-icon mr-1"
                :type="MXS_OBJ_TYPES.LISTENERS"
                :value="state"
            />
            <span v-mxs-highlighter="{ keyword: search_keyword, txt: state }">{{ state }} </span>
        </template>

        <template v-slot:header-append-serviceIds>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ servicesLength }}) </span>
        </template>
        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <router-link
                v-for="serviceId in serviceIds"
                :key="serviceId"
                v-mxs-highlighter="{ keyword: search_keyword, txt: serviceId }"
                :to="`/dashboard/services/${serviceId}`"
                class="rsrc-link"
            >
                {{ serviceId }}
            </router-link>
        </template>
    </data-table>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'listeners',
    data() {
        return {
            tableHeaders: [
                { text: 'Listener', value: 'id', autoTruncate: true },
                { text: 'Port', value: 'port' },
                { text: 'Host', value: 'address' },
                { text: 'State', value: 'state' },
                { text: 'Service', value: 'serviceIds', autoTruncate: true },
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
                    : this.$mxs_t('noEntity', { entityName: 'services' })

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
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        setServicesLength(total) {
            this.servicesLength = total
        },
    },
}
</script>
