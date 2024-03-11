<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
defineProps({
  filters: { type: Array, required: true },
  handleVisFilters: { type: Function, required: true },
})

const store = useStore()
const typy = useTypy()

const allFiltersMap = computed(() => store.getters['filters/map'])

function getFilterModule(id) {
  return typy(allFiltersMap.value[id], 'attributes.module').safeString
}
</script>

<template>
  <div class="visualized-filters">
    <TooltipBtn
      class="hide-filter-btn"
      icon
      variant="text"
      size="small"
      density="compact"
      @click="handleVisFilters"
    >
      <template #btn-content>
        <VIcon size="10" color="error" icon="mxs:close" />
      </template>
      {{ $t('hideFilters') }}
    </TooltipBtn>
    <div class="filter-node-group pt-4 mx-auto" :style="{ width: '75%' }">
      <template v-for="filter in filters.slice().reverse()" :key="filter.id">
        <div class="px-2 py-1 w-100 filter-node d-flex align-center">
          <RouterLink
            target="_blank"
            rel="noopener noreferrer"
            :to="`/dashboard/${filter.type}/${filter.id}`"
            class="pr-2 d-flex"
            :style="{ color: '#fff', flex: 0.5 }"
          >
            <GblTooltipActivator
              :data="{ txt: String(filter.id) }"
              :debounce="0"
              activateOnTruncation
            />
          </RouterLink>
          <GblTooltipActivator
            class="text-right"
            tag="div"
            activateOnTruncation
            :data="{ txt: String(getFilterModule(filter.id)) }"
            :style="{ flex: 0.5 }"
          />
        </div>
        <div class="dashed-arrow d-flex justify-center">
          <span class="line d-inline-block" />
          <VIcon size="12" color="warning" icon="mxs:arrowHead" class="d-block arrow" />
        </div>
      </template>
    </div>
  </div>
</template>

<style lang="scss">
.visualized-filters {
  position: relative;
  .hide-filter-btn {
    position: absolute;
    right: 8px;
    top: 4px;
  }
  .filter-node-group {
    .filter-node {
      background-color: colors.$warning;
      color: #fff;
      border: 1px solid colors.$warning;
      border-radius: 4px;
    }
    .dashed-arrow {
      position: relative;
      height: 24px;
      .line {
        border-right: 2px dashed colors.$warning;
        height: 22px;
      }
      .arrow {
        position: absolute;
        bottom: 0;
      }
    }
  }
}
</style>
