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
const props = defineProps({
  serverInfo: { type: Array, required: true },
  isMaster: { type: Boolean, required: false },
  disabled: { type: Boolean, required: false },
  activatorClass: { type: String, default: '' },
})

const { getMostFreq, getMin } = useHelpers()
const typy = useTypy()
/**
 * If isMaster is true, the component is used to get overall slave replication status
 */
const slaveStatus = computed(() => {
  if (!props.serverInfo.length) return []
  const slaveStats = []
  props.serverInfo.forEach((item) => {
    const repStats = getRepStats(item)
    slaveStats.push({
      id: item.name,
      overall_replication_state: getMostFreq({
        arr: repStats,
        pickBy: 'replication_state',
      }),
      overall_seconds_behind_master: getMin({
        arr: repStats,
        pickBy: 'seconds_behind_master',
      }),
    })
  })
  return slaveStats
})
/**
 * Slave server replication status, serverInfo length is always <= 1
 */
const repStatus = computed(() => getRepStats(typy(props, 'serverInfo[0]').safeObject))

const hasData = computed(() =>
  props.isMaster ? Boolean(slaveStatus.value.length) : Boolean(repStatus.value.length)
)

const isDisabled = computed(() => props.disabled || !hasData.value)
/**
 * Get slave replication status
 * @param {object} serverInfo
 * @returns {array}- replication status
 */
function getRepStats(serverInfo) {
  if (!serverInfo || !serverInfo.slave_connections.length) return []
  const repStats = []
  serverInfo.slave_connections.forEach((slave_conn) => {
    const { seconds_behind_master, slave_io_running, slave_sql_running } = slave_conn
    let replication_state = 'Lagging'
    // Determine replication_state (Stopped||Running||Lagging)
    if (slave_io_running === 'No' || slave_sql_running === 'No') replication_state = 'Stopped'
    else if (seconds_behind_master === 0) {
      if (slave_sql_running === 'Yes' && slave_io_running === 'Yes') replication_state = 'Running'
      else replication_state = slave_io_running !== 'Yes' ? slave_io_running : slave_sql_running
    }
    repStats.push({ name: serverInfo.name, replication_state, ...slave_conn })
  })
  return repStats
}
</script>

<template>
  <VTooltip
    transition="fade-transition"
    content-class="shadow-drop text-navigation py-4 px-3 text-body-2 bg-background rounded-10"
    :max-height="450"
    :disabled="isDisabled"
  >
    <template #activator="{ props }">
      <div
        :class="[activatorClass, isDisabled ? '' : 'd-flex fill-height align-center pointer']"
        v-bind="props"
      >
        <slot name="activator" />
      </div>
    </template>
    <div class="px-1 py-1 font-weight-bold">
      {{ isMaster ? $t('slaveRepStatus') : $t('replicationStatus') }}
    </div>
    <VDivider class="mxs-color-helper border-separator" />

    <template v-if="isMaster">
      <table class="rep-table px-1">
        <tr v-for="(slaveStat, i) in slaveStatus" :key="`${i}`" class="mb-1">
          <td>
            <StatusIcon
              size="13"
              class="mr-1 rep-icon"
              type="replication"
              :value="$typy(slaveStat, 'overall_replication_state').safeString"
            />
          </td>
          <td>
            <div class="d-flex align-center fill-height">
              <GblTooltipActivator
                activateOnTruncation
                :data="{ txt: `${slaveStat.id}` }"
                :maxWidth="300"
              />
              <span class="ml-1 text-grayed-out">
                (+{{ slaveStat.overall_seconds_behind_master }}s)
              </span>
            </div>
          </td>
        </tr>
      </table>
    </template>
    <table v-else class="rep-table px-1">
      <tbody
        v-for="(stat, i) in repStatus"
        :key="`${i}`"
        :class="{ 'tbody-src-replication': !isMaster }"
      >
        <tr v-for="(value, key) in stat" :key="`${key}`">
          <td class="pr-5">
            {{ key }}
          </td>
          <td>
            <div class="d-flex align-center fill-height">
              <StatusIcon
                v-if="key === 'replication_state'"
                size="13"
                class="mr-1 rep-icon"
                type="replication"
                :value="value"
              />
              <GblTooltipActivator
                activateOnTruncation
                :data="{ txt: `${value}`, nudgeTop: 10 }"
                :maxWidth="400"
              />
            </div>
          </td>
        </tr>
      </tbody>
    </table>
  </VTooltip>
</template>

<style lang="scss" scoped>
.tbody-src-replication {
  &:not(:last-of-type) {
    &::after,
    &:first-of-type::before {
      content: '';
      display: block;
      height: 12px;
    }
  }
}
.rep-table {
  td {
    white-space: nowrap;
    height: 24px;
    line-height: 1.5;
  }
}
</style>
