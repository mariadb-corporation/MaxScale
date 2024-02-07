<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { sideBarRoutes } from '@/router/routes'
import NavItem from '@/layouts/NavItem.vue'

const store = useStore()
const router = useRouter()
const route = useRoute()

const maxscale_version = computed(() => store.state.maxscale.maxscale_version)
const isAdmin = computed(() => store.getters['user/isAdmin'])

const currentPath = computed(() => route.path)
const routes = computed(() =>
  isAdmin ? sideBarRoutes : sideBarRoutes.filter((item) => !item.meta.requiredAdmin)
)
const topItems = computed(() => routes.value.filter((item) => !item.meta.isBottom))
const bottomItems = computed(() => routes.value.filter((item) => item.meta.isBottom))

const rail = ref(true)

function navigate(nxtRoute) {
  const { path, meta } = nxtRoute
  if (meta.external) {
    let url = meta.external
    if (url === 'document') {
      const parts = maxscale_version.split('.')
      const ver = `${parts[0]}-${parts[1]}` //  e.g. 23-02
      url = `https://mariadb.com/kb/en/mariadb-maxscale-${ver}/`
    }
    window.open(url, '_blank', 'noopener,noreferrer')
  } else {
    /**
     * E.g. Sidebar dashboard path is /dashboard, but it'll be redirected to /dashboard/servers
     * This checks it and prevent redundant navigation
     */
    const isDupRoute = currentPath.value.includes(meta.redirect || path)
    if (path && path !== currentPath.value && !isDupRoute) {
      router.push(path)
    }
  }
}
</script>

<template>
  <VNavigationDrawer
    class="main-nav"
    width="210"
    rail-width="50"
    permanent
    expand-on-hover
    v-model:rail="rail"
    location="left"
  >
    <VList class="pa-0">
      <NavItem
        v-for="item in topItems"
        :key="item.label"
        :item="item"
        :currentPath="currentPath"
        :rail="rail"
        @click="navigate(item)"
      />
    </VList>
    <template #append>
      <VList class="bottom-nav">
        <NavItem
          v-for="item in bottomItems"
          :key="item.label"
          :item="item"
          :currentPath="currentPath"
          :rail="rail"
          @click="navigate(item)"
        />
      </VList>
    </template>
  </VNavigationDrawer>
</template>
