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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'
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
        ...mapGetters({
            searchKeyWord: 'searchKeyWord',
            allServers: 'server/allServers',
            allSessions: 'session/allSessions',
            allServices: 'service/allServices',
        }),
    },
    watch: {
        $route: function(to) {
            this.activeTab = to
        },
    },
    methods: {
        getTotal(name) {
            let total = null
            switch (name) {
                case 'servers':
                    total = this.allServers.length
                    break
                case 'services':
                    total = this.allServices.length
                    break
                case 'sessions':
                    total = this.allSessions.length
            }
            return total
        },
    },
}
</script>
