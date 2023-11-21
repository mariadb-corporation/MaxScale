<template>
    <v-navigation-drawer
        :mini-variant.sync="isMini"
        color="white"
        class="main-nav"
        width="210"
        mini-variant-width="50"
        fixed
        left
        clipped
        app
        permanent
        @mouseover.native="isMini = false"
        @mouseout.native="isMini = true"
    >
        <v-list class="pa-0">
            <nav-item
                v-for="item in topItems"
                :key="item.label"
                :item="item"
                :currentPath="currentPath"
                :isMini="isMini"
                @click.native="navigate(item)"
            />
        </v-list>
        <template v-slot:append>
            <v-list class="bottom-nav">
                <nav-item
                    v-for="item in bottomItems"
                    :key="item.label"
                    :item="item"
                    :currentPath="currentPath"
                    :isMini="isMini"
                    @click.native="navigate(item)"
                />
            </v-list>
        </template>
    </v-navigation-drawer>
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
import { mapState, mapGetters } from 'vuex'
import NavItem from '@rootSrc/layouts/NavItem.vue'
import { sideBarRoutes } from '@rootSrc/router/routes'

export default {
    name: 'navigation',
    components: { NavItem },
    data() {
        return {
            isMini: true,
        }
    },
    computed: {
        ...mapState({ maxscale_version: state => state.maxscale.maxscale_version }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        currentPath() {
            return this.$route.path
        },
        routes() {
            if (this.isAdmin) return sideBarRoutes
            return sideBarRoutes.filter(item => !item.meta.requiredAdmin)
        },
        topItems() {
            return this.routes.filter(item => !item.meta.isBottom)
        },
        bottomItems() {
            return this.routes.filter(item => item.meta.isBottom)
        },
    },

    methods: {
        navigate(nxtRoute) {
            const { path, meta } = nxtRoute
            if (meta.external) {
                let url = meta.external
                if (url === 'document') {
                    const parts = this.maxscale_version.split('.')
                    const ver = `${parts[0]}-${parts[1]}` //  e.g. 23-02
                    url = `https://mariadb.com/kb/en/mariadb-maxscale-${ver}/`
                }
                window.open(url, '_blank', 'noopener,noreferrer')
            } else {
                /**
                 * E.g. Sidebar dashboard path is /dashboard, but it'll be redirected to /dashboard/servers
                 * This checks it and prevent redundant navigation
                 */
                const isDupRoute = this.currentPath.includes(meta.redirect || path)
                if (path && path !== this.currentPath && !isDupRoute) {
                    this.$router.push(path)
                }
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.main-nav {
    z-index: 7;
    ::v-deep.v-navigation-drawer__border {
        background-color: $separator !important;
    }
}
</style>
