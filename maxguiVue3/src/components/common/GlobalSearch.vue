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
const store = useStore()
const search = computed({
  get() {
    return store.state.search_keyword
  },
  set(v) {
    store.commit('SET_SEARCH_KEYWORD', v)
  },
})
</script>

<template>
  <VTextField
    data-test="search"
    v-model.trim="search"
    class="global-search"
    :height="36"
    variant="outlined"
    :placeholder="$t('search')"
    hide-details
    rounded
  >
    <template #append-inner>
      <VIcon size="16" icon="mxs:search" />
    </template>
  </VTextField>
</template>

<style lang="scss" scoped>
.global-search {
  max-width: 175px;
}
</style>
