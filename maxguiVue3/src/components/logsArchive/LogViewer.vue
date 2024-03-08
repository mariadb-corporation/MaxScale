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
import { DynamicScroller, DynamicScrollerItem } from 'vue-virtual-scroller'
import 'vue-virtual-scroller/dist/vue-virtual-scroller.css'
import LogLine from '@/components/logsArchive/LogLine.vue'
import { fromUnixTime, isToday } from 'date-fns'

const props = defineProps({ height: { type: Number, required: true } })

const store = useStore()
const typy = useTypy()
const {
  lodash: { unionBy, pickBy },
} = useHelpers()

let connection = null
let isInitializing = ref(false)
let isScrollable = ref(false)
let isFetching = ref(false)
let reachedTopLine = ref(false)
let logs = ref([])
let prevLogData = ref([])
let isNotifShown = ref(false)
let isAtBottom = ref(false)
const virtualListRef = ref(null)

const prev_log_link = computed(() => store.state.logs.prev_log_link)
const prev_logs = computed(() => store.state.logs.prev_logs)
const latest_logs = computed(() => store.state.logs.latest_logs)
const log_filter = computed(() => store.state.logs.log_filter)
const priorities = computed(() => log_filter.value.priorities)
const logDateRangeTimestamp = computed(() => store.getters['logs/logDateRangeTimestamp'])

watch(prev_log_link, (v) => (reachedTopLine.value = Boolean(!v)))
watch(
  prev_logs,
  async (v) => {
    prevLogData.value = v
  },
  { deep: true }
)
watch(
  log_filter,
  async (v) => {
    if (!typy(v).isEmptyObject) await handleFetchLogs()
  },
  { deep: true }
)

onBeforeMount(async () => {
  isInitializing.value = true
  await handleFetchLogs()
  openConnection()
  isInitializing.value = false
})
onBeforeUnmount(() => disconnect())

async function handleFetchLogs() {
  await getLatestLogs()
  await fetchLogsUntilScrollable()
  nextTick(() => setVirtualListToBottom())
}

async function getLatestLogs() {
  isFetching.value = true
  await store.dispatch('logs/fetchLatestLogs')
  isFetching.value = false
  logs.value = Object.freeze(latest_logs.value)
}

/**
 * This function fetches previous logs until the log container div
 * is scrollable. This allows user to scroll up to get older logs
 */
async function fetchLogsUntilScrollable() {
  detectScrollability()
  if (!isScrollable.value) {
    while (!isScrollable.value && !reachedTopLine.value) {
      isFetching.value = true
      await fetchAndPrependPrevLogs()
      nextTick(() => detectScrollability())
    }
    isFetching.value = false
  }
}

/**
 * @param {boolean} [param.loop] - if true, it loops the request until getting some logs in case
 * prevLogData is an empty array.
 */
async function fetchAndPrependPrevLogs({ loop = false } = {}) {
  if (reachedTopLine.value) return
  else await store.dispatch('logs/fetchPrevLogs')

  if (prevLogData.value.length) {
    const currentTopId = typy(logs.value, '[0].id').safeString
    // using union because prev logs may contain some log entries that are already in logs
    logs.value = unionBy([], prevLogData.value, logs.value, 'id')
    nextTick(() => preserveScrollHeight(currentTopId))
    prevLogData.value = [] // clear logs as it has been prepended to logs
  }

  // loop until getting some logs
  else if (loop) await fetchAndPrependPrevLogs({ loop })
}

/**
 * This function opens websocket connection to get real-time logs
 */
function openConnection() {
  const { protocol, host } = window.location
  const socketProtocol = protocol === 'http:' ? 'ws' : 'wss'
  const socketURI = `${socketProtocol}://${host}/maxscale/logs/stream`

  connection = new WebSocket(socketURI)

  // push new log to logs
  connection.onmessage = (e) => {
    const newEntry = JSON.parse(e.data)
    /**
     * logs/stream endpoint doesn't return log entry with the same data structure as /logs/entries,
     * so it should be transformed to the right structure
     */
    const logEntry = {
      id: newEntry.id,
      attributes: pickBy(newEntry, (value, key) => key !== 'id'),
    }
    if (isMatchedFilter(logEntry)) logs.value = Object.freeze([...logs.value, logEntry])
    if (isMatchedFilter(logEntry)) nextTick(() => showNotifHandler())
  }
}

function disconnect() {
  if (connection) connection.close()
  logs.value = []
  prevLogData.value = []
}

async function onScroll(e) {
  if (e.target.scrollHeight > props.height) {
    if (checkIsAtBottom(e)) {
      isAtBottom.value = true
      isNotifShown.value = false // Turn off notif if it is at bottom already
    } else isAtBottom.value = false
  }
  if (e.target.scrollTop === 0) await onTotop()
}

async function onTotop() {
  if (isFetching.value || reachedTopLine.value) return
  isFetching.value = true
  await fetchAndPrependPrevLogs({ loop: true })
  isFetching.value = false
}

/* if scrolled position is at bottom position before new logs are appended,
 * scroll to bottom to see latest data. Otherwise, how notification button
 * (let user controls scroll to bottom)
 */
function showNotifHandler() {
  if (isAtBottom.value) setVirtualListToBottom()
  else isNotifShown.value = true
}

function detectScrollability() {
  isScrollable.value = typy(virtualListRef.value.$el, 'scrollHeight').safeNumber > props.height
}

function setVirtualListToBottom() {
  if (virtualListRef.value) virtualListRef.value.scrollToBottom()
}

/**
 * @param {Object} e - scroll event
 * @returns {boolean}
 */
function checkIsAtBottom(e) {
  // Add a tolerance value to handle potential decimal discrepancies
  const tolerance = 1
  return props.height + e.target.scrollTop + tolerance >= e.target.scrollHeight
}

/**
 * If the `timestamp` falls within the current date, the condition evaluates to true,
 * even the `timestamp` is greater than logDateRangeTimestamp `to` value.
 * @param {number} timestamp unix timestamp in seconds
 * @returns {boolean}
 */
function isBetweenTimeRange(timestamp) {
  const [from, to] = logDateRangeTimestamp.value
  return timestamp >= from && (timestamp <= to || isToday(fromUnixTime(to)))
}

/**
 * @param {Object} log - log object
 * @returns {boolean}
 */
function isMatchedFilter({ attributes: { priority, unix_timestamp } }) {
  return (
    isBetweenTimeRange(unix_timestamp) &&
    (priorities.value.includes(priority) || !priorities.value.length)
  )
}

/**
 * @param {string} id - id of item
 */
function preserveScrollHeight(id) {
  const index = logs.value.findIndex((item) => item.id === id)
  const virtualList = virtualListRef.value
  virtualList.scrollToItem(index)
}
</script>

<template>
  <div class="relative" :style="{ height: isInitializing ? 'unset' : `${height}px` }">
    <DynamicScroller
      ref="virtualListRef"
      class="fill-height"
      :items="logs"
      :min-item-size="24"
      @scroll="onScroll"
    >
      <template #before>
        <template v-if="isFetching">
          <VProgressCircular
            v-show="isScrollable"
            color="primary"
            indeterminate
            size="24"
            class="d-flex mx-auto mb-2"
          />
          <p class="text-center mt-1 loading-logs-text-indicator text-grayed-out mb-4">
            {{ $t('loadingLogs') }}...
          </p>
        </template>
        <code v-else-if="reachedTopLine && !logs.length" class="d-block mariadb-code-style">
          {{ $t('noLogsFound') }}
        </code>
      </template>
      <template #default="{ item, index, active }">
        <DynamicScrollerItem
          :item="item"
          :active="active"
          :size-dependencies="[item.message]"
          :data-index="index"
        >
          <LogLine :key="item.id" :item="item" />
        </DynamicScrollerItem>
      </template>
    </DynamicScroller>
    <VBtn
      v-if="isNotifShown"
      class="absolute new-log-btn font-weight-medium px-7 text-capitalize"
      color="primary"
      rounded
      variant="flat"
      @click="setVirtualListToBottom"
    >
      {{ $t('newMessagesAvailable') }}!
      <VIcon class="rotate-down" size="32" icon="$mdiChevronDown" />
    </VBtn>
  </div>
</template>

<style lang="scss" scoped>
.new-log-btn {
  right: 50%;
  transform: translateX(50%);
  bottom: 0;
}
</style>
