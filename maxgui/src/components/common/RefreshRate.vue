<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({ onCountDone: { type: Function, default: () => null } })

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const route = useRoute()

const items = [
  ...[5, 10, 20, 30, 60, 120, 300].map((s) => ({ label: t('seconds', { n: s }), value: s })),
  { label: t('noRefresh'), value: -1 },
]

const refreshCount = ref(10)
const paused = ref(false)

const currRefreshRate = computed(() => store.getters['persisted/currRefreshRate'])
const group = computed(() => typy(route, 'meta.group').safeString)
const refreshRate = computed({
  get: () => currRefreshRate.value,
  set: (v) => {
    if (v !== currRefreshRate.value)
      store.commit('persisted/UPDATE_REFRESH_RATE_BY_ROUTE_GROUP', {
        group: group.value,
        payload: v,
      })
  },
})

watch(refreshRate, (v) => (refreshCount.value = v))
watch(refreshCount, async (v) => {
  if (v === 0) {
    // pause until finish calling onCountDone
    paused.value = true
    await props.onCountDone()
    paused.value = false
  }
})

let workerTimer
onMounted(() => {
  refreshCount.value = refreshRate.value
  workerInit()
})
onBeforeUnmount(() => destroyTimer())
function destroyTimer() {
  workerTimer && workerTimer.terminate()
}

function updateCountTime() {
  if (refreshCount.value > 0) refreshCount.value = refreshCount.value - 1
  else if (refreshCount.value === 0) refreshCount.value = refreshRate.value
}

function workerInit() {
  workerTimer = new Worker(new URL('@/utils/workerTimer.js', import.meta.url))
  workerTimer.postMessage([{ name: 'Next refresh timer', interval: 1000 }])
  workerTimer.onmessage = () => {
    if (!paused.value) updateCountTime()
  }
}

function onSelectRefreshRate(item) {
  refreshRate.value = item.value
  refreshCount.value = refreshRate.value
}
</script>

<template>
  <VMenu transition="slide-y-transition" content-class="full-border" :max-width="200">
    <template #activator="{ props: menu }">
      <VTooltip location="top">
        <template #activator="{ props: tooltip }">
          <VBtn
            class="text-none font-weight-regular refresh-rate-btn px-2"
            variant="text"
            color="primary"
            v-bind="{ ...tooltip, ...menu }"
          >
            <VIcon class="mr-1" size="12" color="primary" icon="mxs:reload" />
            <span class="mr-1 text-navigation">
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
        v-for="item in items"
        :key="item.value"
        dense
        link
        @click="onSelectRefreshRate(item)"
      >
        <VListItemTitle class="text-text">
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
