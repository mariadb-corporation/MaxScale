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
import QueryConn from '@wsModels/QueryConn'
import { abortRequests } from '@/utils/axios'

const store = useStore()

const isProfileOpened = ref(false)
const loggedInUser = computed(() => store.state.users.logged_in_user)

async function handleLogout() {
  abortRequests() // abort all previous pending requests before logging out
  // Disconnect all workspace connections
  await QueryConn.dispatch('disconnectAll')
  await store.dispatch('users/logout')
}
</script>

<template>
  <VAppBar :height="64" class="pl-8 pr-4" flat color="blue-azure">
    <VToolbarTitle class="text-h5">
      <RouterLink to="/dashboard/servers" class="text-decoration-none">
        <img class="logo" src="@/assets/logo.svg" alt="MariaDB Logo" />
        <span class="product-name relative tk-azo-sans-web font-weight-medium text-background">
          MaxScale
        </span>
      </RouterLink>
    </VToolbarTitle>
    <VSpacer />
    <VBtn class="arrow-toggle mr-0 rounded-0" text>
      <VIcon class="mr-1" size="30"> mxs:user </VIcon>
      <span class="user-name tk-adrianna text-capitalize font-weight-regular">
        {{ $typy(loggedInUser, 'name').safeString }}
      </span>
      <VIcon
        :class="[isProfileOpened ? 'rotate-up' : 'rotate-down']"
        size="14"
        class="mr-0 ml-1"
        left
      >
        mxs:arrowDown
      </VIcon>
      <VMenu v-model="isProfileOpened" activator="parent" content-class="rounded-0">
        <VList class="pa-0">
          <VListItem @click="handleLogout">
            <VListItemTitle>{{ $t('logout') }}</VListItemTitle>
          </VListItem>
        </VList>
      </VMenu>
    </VBtn>
  </VAppBar>
</template>

<style lang="scss" scoped>
.logo {
  vertical-align: middle;
  width: 155px;
  height: 38px;
}
.product-name {
  vertical-align: middle;
  font-size: 1.125rem;
}
.user-name {
  font-size: 1rem;
}
.VBtn {
  letter-spacing: normal;
}
</style>
