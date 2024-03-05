<script setup>
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
import { MXS_OBJ_TYPES } from '@/constants'
import ConfigSync from '@/components/dashboard/ConfigSync.vue'

defineProps({
  onCountDone: { type: Function, required: true },
})

const store = useStore()
const route = useRoute()
const { uptimeHumanize, copyTextToClipboard } = useHelpers()

const maxscale_overview_info = computed(() => store.state.maxscale.maxscale_overview_info)
const config_sync = computed(() => store.state.maxscale.config_sync)

const mxsInfo = computed(() => {
  const { commit, started_at, activated_at } = maxscale_overview_info.value
  return {
    commit,
    started_at,
    activated_at,
  }
})
const activeTabName = computed(() => route.path.split('/').at(-1))

let isCopied = ref(false)
let uptime = ref(0)
let copyState = ref('Double click to copy to clipboard')

const defFormType = computed(() => {
  const { SERVERS, SERVICES, LISTENERS, FILTERS } = MXS_OBJ_TYPES
  switch (activeTabName.value) {
    case SERVERS:
    case SERVICES:
    case LISTENERS:
    case FILTERS:
      return activeTabName.value
    default:
      return SERVICES
  }
})
const humanizedUptime = computed(() => {
  return uptimeHumanize(uptime.value)
})

watch(isCopied, (v) => {
  if (v) {
    copyState.value = 'Copied'
    setTimeout(
      () => ((isCopied.value = false), (copyState.value = 'Double click to copy to clipboard')),
      2000
    )
  }
})
watch(
  () => maxscale_overview_info.value.uptime,
  (v) => {
    uptime.value = v
    initTimer()
  }
)
let workerTimer
function updateUpTime() {
  uptime.value++
}

function initTimer() {
  workerTimer = new Worker(new URL('@/utils/workerTimer.js', import.meta.url))
  workerTimer.postMessage([{ name: 'MaxScale uptime worker timer', interval: 1000 }])
  workerTimer.onmessage = () => {
    updateUpTime()
  }
}
function copyToClipboard(txt) {
  copyTextToClipboard(txt)
  isCopied.value = true
}

onBeforeUnmount(() => workerTimer && workerTimer.terminate())
</script>
<template>
  <div class="pb-6 d-flex align-center">
    <portal to="view-header__right">
      <RefreshRate :onCountDone="onCountDone" />
      <GlobalSearch class="ml-4 d-inline-block" />
      <CreateMxsObj class="ml-4 d-inline-block" :defFormType="defFormType" />
    </portal>
    <ConfigSync v-if="config_sync" :data="config_sync" className="mr-5" />
    <pre class="grayed-out-info text-capitalize">{{ $t('uptime') }}{{ humanizedUptime }}</pre>
    <VMenu
      transition="slide-y-transition"
      :close-on-content-click="false"
      open-on-hover
      content-class="rounded-10 with-arrow with-arrow--top-left no-border shadow-drop"
      offset="0 20"
    >
      <template #activator="{ props }">
        <VIcon
          class="ml-1 pointer"
          size="16"
          color="#9DB4BB"
          icon="$mdiInformationOutline"
          v-bind="props"
        />
      </template>
      <VSheet class="px-6 py-6" max-width="320px">
        <span class="d-block mb-1 text-body-2 font-weight-bold text-capitalize">
          {{ $t('aboutMaxScale') }}
        </span>
        <div v-for="(value, name) in mxsInfo" :key="name">
          <span class="d-flex text-body-2">
            <span class="text-capitalize" style="width: 35%">
              {{ name.split('_').join(' ') }}
            </span>
            <VTooltip
              v-if="name === 'commit'"
              :key="copyState"
              transition="slide-y-reverse-transition"
              top
            >
              <template #activator="{ props }">
                <div
                  style="width: 65%"
                  class="pointer d-inline-block text-truncate"
                  @dblclick="copyToClipboard(value)"
                  v-bind="props"
                >
                  {{ value }}
                </div>
              </template>
              {{ copyState }}
            </VTooltip>
            <div
              v-else-if="value && (name === 'started_at' || name === 'activated_at')"
              style="width: 65%"
              class="d-inline-block"
            >
              {{
                $helpers.dateFormat({
                  value,
                  formatType: 'dd MMM yyyy HH:mm:ss',
                })
              }}
            </div>
            <div v-else style="width: 65%" class="d-inline-block">
              {{ value }}
            </div>
          </span>
        </div>
      </VSheet>
    </VMenu>
  </div>
</template>

<style lang="scss" scoped>
.page-header--left {
  .page-title {
    margin-bottom: 0px;
    line-height: normal;
  }
}
</style>
