<script>
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

import { mapActions, mapGetters } from 'vuex'
import PageHeader from '@/components/dashboard/PageHeader.vue'
import DashboardGraphs from '@/components/dashboard/DashboardGraphs.vue'
import ServersTabItem from '@/components/dashboard/ServersTabItem.vue'

export default {
  components: {
    PageHeader,
    DashboardGraphs,
    ServersTabItem,
  },
  data() {
    return {
      activeTab: null,
      test: 12,
    }
  },
  computed: {
    ...mapGetters({
      getTotalFilters: 'filter/getTotalFilters',
      getTotalListeners: 'listener/getTotalListeners',
      getTotalServers: 'server/getTotalServers',
      getTotalServices: 'service/getTotalServices',
      getTotalSessions: 'session/getTotalSessions',
    }),
    tabs() {
      return ['servers', 'sessions', 'services', 'listeners', 'filters']
    },
  },
  async created() {
    await this.fetchMaxScaleOverviewInfo()
    await this.fetchAll()
    // Init graph datasets
    await this.$typy(this.$refs, 'graphs.initDatasets').safeFunction()
  },
  methods: {
    ...mapActions({
      fetchMaxScaleOverviewInfo: 'maxscale/fetchMaxScaleOverviewInfo',
      fetchConfigSync: 'maxscale/fetchConfigSync',
      fetchThreadStats: 'maxscale/fetchThreadStats',
      fetchAllServers: 'server/fetchAllServers',
      fetchAllMonitors: 'monitor/fetchAllMonitors',
      fetchSessions: 'session/fetchSessions',
      fetchAllServices: 'service/fetchAllServices',
      fetchAllListeners: 'listener/fetchAllListeners',
      fetchAllFilters: 'filter/fetchAllFilters',
    }),
    async fetchAll() {
      await Promise.all([
        this.fetchThreadStats(),
        this.fetchAllServers(),
        this.fetchAllMonitors(),
        this.fetchSessions(),
        this.fetchAllServices(),
        this.fetchAllListeners(),
        this.fetchAllFilters(),
        this.fetchConfigSync(),
      ])
    },
    async onCountDone() {
      const timestamp = Date.now()
      await Promise.all([
        this.fetchAll(),
        this.$typy(this.$refs, 'graphs.updateChart').safeFunction(timestamp),
      ])
    },
    getTotal(name) {
      return this[`getTotal${this.$helpers.capitalizeFirstLetter(name)}`]
    },
  },
}
</script>
<template>
  <ViewWrapper>
    <VSheet>
      <PageHeader :onCountDone="onCountDone" />
      <DashboardGraphs ref="graphs" />
      <VTabs v-model="activeTab">
        <VTab v-for="name in tabs" :key="name" :to="`/dashboard/${name}`">
          {{ $t(name === 'sessions' ? 'currentSessions' : name, 2) }}
          <span class="grayed-out-info"> ({{ getTotal(name) }}) </span>
        </VTab>
      </VTabs>
      <VWindow v-model="activeTab" class="fill-height">
        <VWindowItem v-for="name in tabs" :key="name" class="pt-2">
          <component :is="`${$helpers.capitalizeFirstLetter(name)}TabItem`" />
        </VWindowItem>
      </VWindow>
    </VSheet>
  </ViewWrapper>
</template>
