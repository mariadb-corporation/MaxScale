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
    <h6 class="text-h6 etl-logs-title text-navigation mb-4">{{ $t('msgLog') }}</h6>
    <code
      ref="logCtrRef"
      class="fill-height log-container overflow-y-auto rounded mxs-helper-class all-border-separator pa-4"
    >
      <div v-for="log in logs" :key="log.timestamp">
        <span class="text-grayed-out mr-2">
          {{
            $helpers.dateFormat({
              value: log.timestamp,
              formatType: 'dd-MMM-yyyy  HH:mm:ss',
            })
          }}
        </span>
        <span class="log-name">{{ log.name }}</span>
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
  .log-name {
    white-space: pre-wrap;
  }
}
</style>
