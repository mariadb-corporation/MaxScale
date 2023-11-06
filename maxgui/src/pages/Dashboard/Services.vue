<template>
    <data-table
        :search="search_keyword"
        :headers="tableHeaders"
        :data="tableRows"
        :sortDesc="false"
        sortBy="id"
        :itemsPerPage="-1"
        :customFilter="customFilter"
    >
        <template v-slot:id="{ data: { item: { id } } }">
            <router-link
                :key="id"
                v-mxs-highlighter="{ keyword: search_keyword, txt: id }"
                :to="`/dashboard/services/${id}`"
                class="rsrc-link"
            >
                {{ id }}
            </router-link>
        </template>
        <template v-slot:state="{ data: { item: { state } } }">
            <icon-sprite-sheet
                size="16"
                class="service-state-icon mr-1"
                :frame="$helpers.serviceStateIcon(state)"
            >
                services
            </icon-sprite-sheet>
            <span v-mxs-highlighter="{ keyword: search_keyword, txt: state }">{{ state }} </span>
        </template>

        <template v-slot:header-append-routingTargets>
            <span class="ml-1 mxs-color-helper text-grayed-out">
                ({{ routingTargetsLength }})
            </span>
        </template>
        <template v-slot:routingTargets="{ data: { item: { routingTargets } } }">
            <span
                v-if="typeof routingTargets === 'string'"
                v-mxs-highlighter="{ keyword: search_keyword, txt: routingTargets }"
            >
                {{ routingTargets }}
            </span>

            <template v-else-if="routingTargets.length === 1">
                <router-link
                    v-mxs-highlighter="{ keyword: search_keyword, txt: routingTargets[0].id }"
                    :to="`/dashboard/${routingTargets[0].type}/${routingTargets[0].id}`"
                    class="rsrc-link"
                >
                    {{ routingTargets[0].id }}
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
                        class="pointer mxs-color-helper text-anchor override-td--padding disable-auto-truncate"
                        v-on="on"
                    >
                        {{ routingTargets.length }}
                        targets
                    </div>
                </template>

                <v-sheet class="pa-4">
                    <router-link
                        v-for="target in routingTargets"
                        :key="target.id"
                        v-mxs-highlighter="{ keyword: search_keyword, txt: target.id }"
                        :to="`/dashboard/${target.type}/${target.id}`"
                        class="text-body-2 d-block rsrc-link"
                    >
                        {{ target.id }}
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'

export default {
    name: 'services',

    data() {
        return {
            tableHeaders: [
                { text: 'Service', value: 'id', autoTruncate: true },
                { text: 'State', value: 'state' },
                { text: 'Router', value: 'router' },
                { text: 'Current Sessions', value: 'connections', autoTruncate: true },
                { text: 'Total Sessions', value: 'total_connections', autoTruncate: true },
                {
                    text: this.$mxs_t('routingTargets'),
                    value: 'routingTargets',
                    autoTruncate: true,
                },
            ],
            routingTargetsLength: 0,
        }
    },

    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            all_services: state => state.service.all_services,
            ROUTING_TARGET_RELATIONSHIP_TYPES: state =>
                state.app_config.ROUTING_TARGET_RELATIONSHIP_TYPES,
        }),

        /**
         * @return {Array} An array of objects
         */
        tableRows() {
            let rows = []
            let allRoutingTargets = []
            this.all_services.forEach(service => {
                const {
                    id,
                    attributes: { state, router, connections, total_connections },
                    relationships = {},
                } = service || {}

                const targets = Object.keys(relationships).reduce((arr, type) => {
                    if (this.ROUTING_TARGET_RELATIONSHIP_TYPES.includes(type)) {
                        arr = [...arr, ...this.$typy(relationships[type], 'data').safeArray]
                    }
                    return arr
                }, [])
                const routingTargets = targets.length
                    ? targets
                    : this.$mxs_t('noEntity', { entityName: this.$mxs_t('routingTargets') })

                if (typeof routingTargets !== 'string')
                    allRoutingTargets = [...allRoutingTargets, ...routingTargets]

                const row = { id, state, router, connections, total_connections, routingTargets }
                rows.push(row)
            })
            const uniqueRoutingTargetIds = new Set(allRoutingTargets.map(target => target.id))
            this.setRoutingTargetsLength([...uniqueRoutingTargetIds].length)
            return rows
        },
    },
    methods: {
        setRoutingTargetsLength(total) {
            this.routingTargetsLength = total
        },
        customFilter(v, search) {
            let value = `${v}`
            // filter for routingTargets
            if (this.$typy(v).isArray) value = v.map(v => v.id).join(', ')
            return this.$helpers.ciStrIncludes(value, search)
        },
    },
}
</script>
