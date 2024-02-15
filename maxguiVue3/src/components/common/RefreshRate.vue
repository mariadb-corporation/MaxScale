<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'

export default {
  props: {
    onCountDone: { type: Function, default: () => null },
  },
  data() {
    return {
      refreshCount: 10,
      chosenRefreshRate: 10,
      seconds: [5, 10, 20, 30, 60, 120, 300],
      paused: false,
    }
  },
  computed: {
    ...mapState({
      refresh_rate_by_route_group: (state) => state.persisted.refresh_rate_by_route_group,
    }),
    ...mapGetters({ currRefreshRate: 'currRefreshRate' }),
    refreshRateOpts() {
      return [
        ...this.seconds.map((s) => ({
          label: this.$t('seconds', { n: s }),
          value: s,
        })),
        { label: this.$t('noRefresh'), value: -1 },
      ]
    },
    group() {
      return this.$typy(this.$route, 'meta.group').safeString
    },
    refreshRate: {
      get() {
        return this.currRefreshRate
      },
      set(v) {
        if (v !== this.currRefreshRate)
          this.UPDATE_REFRESH_RATE_BY_ROUTE_GROUP({ group: this.group, payload: v })
      },
    },
  },
  watch: {
    refreshRate() {
      this.refreshCount = this.refreshRate
    },
    async refreshCount(v) {
      if (v === 0) {
        // pause until finish the handler for asyncEmit event
        this.paused = true
        await this.onCountDone()
        this.paused = false
      }
    },
  },
  mounted() {
    this.refreshCount = this.refreshRate
    this.workerInit()
  },
  beforeUnmount() {
    this.destroyTimer()
  },
  methods: {
    ...mapMutations({
      UPDATE_REFRESH_RATE_BY_ROUTE_GROUP: 'persisted/UPDATE_REFRESH_RATE_BY_ROUTE_GROUP',
    }),
    destroyTimer() {
      this.workerTimer && this.workerTimer.terminate()
    },
    updateCountTime() {
      if (this.refreshCount > 0) this.refreshCount = this.refreshCount - 1
      else if (this.refreshCount === 0) this.refreshCount = this.refreshRate
    },
    workerInit() {
      this.workerTimer = new Worker(new URL('@/utils/workerTimer.js', import.meta.url))
      this.workerTimer.postMessage([{ name: 'Next refresh timer', interval: 1000 }])
      this.workerTimer.onmessage = () => {
        if (!this.paused) this.updateCountTime()
      }
    },
    onSelectRefreshRate(item) {
      this.refreshRate = item.value
      this.refreshCount = this.refreshRate
    },
  },
}
</script>

<template>
  <VMenu transition="slide-y-transition" content-class="full-border" :max-width="200">
    <template #activator="{ props: menu }">
      <VTooltip location="top" transition="slide-y-transition">
        <template #activator="{ props: tooltip }">
          <VBtn
            class="text-none font-weight-regular refresh-rate-btn px-2"
            variant="text"
            size="small"
            color="primary"
            v-bind="{ ...tooltip, ...menu }"
          >
            <VIcon class="mr-1" size="12" color="primary" icon="mxs:reload" />
            <span class="mr-1 mxs-color-helper text-navigation">
              {{ $t('nextRefresh') }}:
              <template v-if="refreshCount >= 0">
                {{ $t('seconds', { n: refreshCount }) }}
              </template>
              <template v-else>{{ $t('noRefresh') }} </template>
            </span>
          </VBtn>
        </template>
        {{ $t('info.nextRefresh') }}
      </VTooltip>
    </template>
    <VList>
      <VListItem
        v-for="item in refreshRateOpts"
        :key="item.value"
        dense
        link
        @click="onSelectRefreshRate(item)"
      >
        <VListItemTitle class="mxs-color-helper text-text">
          {{ item.label }}
        </VListItemTitle>
      </VListItem>
    </VList>
  </VMenu>
</template>

<style lang="scss" scoped>
.refresh-rate-label {
  font-size: 0.875rem;
}
</style>
