<template>
    <page-wrapper fluid :spacerStyle="{ borderBottom: 'thin solid #e7eef1' }">
        <portal to="page-header">
            <v-tabs v-model="activeTab" class="tab-navigation-wrapper flex-grow-0">
                <v-tab v-for="route in visTabRoutes" :key="route.path" :to="route.path">
                    {{ $t(route.name) }}
                </v-tab>
            </v-tabs>
        </portal>
        <v-sheet class="pt-5 fill-height d-flex flex-column">
            <router-view />
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { visTabRoutes } from 'router/routes'
export default {
    name: 'visualization',
    data() {
        return {
            activeTab: null,
        }
    },
    computed: {
        visTabRoutes() {
            return visTabRoutes
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .tab-navigation-wrapper {
    .v-window,
    .v-window-item {
        height: 100%;
    }
}
</style>
