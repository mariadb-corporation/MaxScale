<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({ task: { type: Object, required: true } })

const typy = useTypy()

const logCtrRef = ref(null)

const etlLog = computed(() => typy(props.task, 'logs').safeObjectOrEmpty)
const activeStageIdx = computed(() => typy(props.task, 'active_stage_index').safeNumber)
const logs = computed(() => typy(etlLog.value, `[${activeStageIdx.value}]`).safeArray)

watch(
  logs,
  (v) => {
    if (v && v.length) scrollToBottom()
  },
  { deep: true }
)

onMounted(() => scrollToBottom())

function scrollToBottom() {
  nextTick(() => {
    let ele = typy(logCtrRef, 'value').safeObject
    if (ele) ele.scrollTop = ele.scrollHeight
  })
}
</script>

<template>
  <div class="d-flex flex-column">
    <h6 class="text-h6 etl-logs-title text-navigation mb-4" data-test="title">
      {{ $t('msgLog') }}
    </h6>
    <code
      ref="logCtrRef"
      class="fill-height log-container overflow-y-auto rounded border--separator pa-4"
    >
      <div v-for="log in logs" :key="log.timestamp">
        <span class="text-grayed-out mr-2" data-test="log-time">
          {{
            $helpers.dateFormat({
              value: log.timestamp,
              formatType: 'dd-MMM-yyyy HH:mm:ss',
            })
          }}
        </span>
        <!-- TODO: Rename property `name` to `txt` -->
        <span class="log-txt" data-test="log-txt">{{ log.name }}</span>
      </div>
    </code>
  </div>
</template>

<style lang="scss" scoped>
.etl-logs-title {
  line-height: normal;
}
.log-container {
  font-size: 0.75rem;
  letter-spacing: -0.1px;
  .log-txt {
    white-space: pre-wrap;
  }
}
</style>
