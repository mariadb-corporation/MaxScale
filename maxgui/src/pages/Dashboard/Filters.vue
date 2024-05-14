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
                :to="`/dashboard/filters/${id}`"
                class="rsrc-link"
            >
                {{ id }}
            </router-link>
        </template>

        <template v-slot:header-append-serviceIds>
            <span class="ml-1 mxs-color-helper text-grayed-out"> ({{ servicesLength }}) </span>
        </template>

        <template v-slot:serviceIds="{ data: { item: { serviceIds } } }">
            <span
                v-if="typeof serviceIds === 'string'"
                v-mxs-highlighter="{ keyword: search_keyword, txt: serviceIds }"
            >
                {{ serviceIds }}
            </span>

            <template v-else-if="serviceIds.length < 2">
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

            <v-menu
                v-else
                top
                offset-y
                transition="slide-y-transition"
                :close-on-content-click="false"
                open-on-hover
                content-class="shadow-drop"
                :min-width="1"
            >
                <template v-slot:activator="{ on }">
                    <div
                        class="pointer mxs-color-helper text-anchor  override-td--padding disable-auto-truncate"
                        v-on="on"
                    >
                        {{ serviceIds.length }}
                        {{ $mxs_tc('services', 2).toLowerCase() }}
                    </div>
                </template>

                <v-sheet class="pa-4">
                    <router-link
                        v-for="serviceId in serviceIds"
                        :key="serviceId"
                        v-mxs-highlighter="{ keyword: search_keyword, txt: serviceId }"
                        :to="`/dashboard/services/${serviceId}`"
                        class="text-body-2 d-block rsrc-link"
                    >
                        {{ serviceId }}
                    </router-link>
                </v-sheet>
            </v-menu>
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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'filters',

    data() {
        return {
            tableHeaders: [
                { text: 'Filter', value: 'id', autoTruncate: true },
                { text: 'Service', value: 'serviceIds', autoTruncate: true },
                { text: 'Module', value: 'module' },
            ],
            servicesLength: 0,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_filters: state => state.filter.all_filters,
        }),

        /**
         * @return {Array} An array of objects
         */
        tableRows: function() {
            let rows = []
            let allServiceIds = []
            this.all_filters.forEach(filter => {
                const {
                    id,
                    attributes: { module: filterModule } = {},
                    relationships: { services: { data: associatedServices = [] } = {} },
                } = filter || {}

                const serviceIds = associatedServices.length
                    ? associatedServices.map(item => `${item.id}`)
                    : this.$mxs_t('noEntity', { entityName: 'services' })

                if (typeof serviceIds !== 'string')
                    allServiceIds = [...allServiceIds, ...serviceIds]

                rows.push({
                    id: id,
                    serviceIds: serviceIds,
                    module: filterModule,
                })
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
