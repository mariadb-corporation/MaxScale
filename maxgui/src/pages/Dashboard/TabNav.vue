<template>
    <v-tabs v-model="activeTab" class="tab-navigation-wrapper">
        <v-tab v-for="route in tabRoutes" :key="route.path" :to="route.path">
            {{ $tc(route.text, 2) }}
            <span class="field-text-info color text-field-text">
                ({{ getTotal(route.name) }})
            </span>
        </v-tab>
        <v-tabs-items v-model="activeTab">
            <v-tab-item v-for="route in tabRoutes" :id="route.path" :key="route.name" class="pt-2">
                <!-- Only render view if tab is active -->
                <router-view v-if="activeTab === route.path" />
            </v-tab-item>
        </v-tabs-items>
    </v-tabs>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import tabRoutes from 'router/tabRoutes'

export default {
    name: 'tab-nav',

    data() {
        return {
            activeTab: null,
            tabRoutes: tabRoutes,
        }
    },
    computed: {
        ...mapState({
            all_filters: state => state.filter.all_filters,
            all_listeners: state => state.listener.all_listeners,
            all_servers: state => state.server.all_servers,
            all_services: state => state.service.all_services,
            all_sessions: state => state.session.all_sessions,
        }),
    },

    methods: {
        getTotal(name) {
            return this[`all_${name}`].length
        },
    },
}
</script>
