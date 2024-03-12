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
import statusIconHelpers from '@/utils/statusIconHelpers'
import { MXS_OBJ_TYPES, MRDB_MON } from '@/constants'
import ClusterServerTooltip from '@/components/visualizer/ClusterServerTooltip.vue'
import { genCluster } from '@/utils/visualization'

const store = useStore()
const router = useRouter()
const typy = useTypy()
const { flattenTree } = useHelpers()

const serverMap = computed(() => store.getters['servers/map'])
const all_monitors = computed(() => store.state.monitors.all_monitors)

const clusterMap = computed(() =>
  all_monitors.value.reduce((acc, monitor) => {
    if (monitor.attributes.module === MRDB_MON)
      acc[monitor.id] = genCluster({ monitor, serverMap: serverMap.value })
    return acc
  }, {})
)

function navToCluster(cluster) {
  router.push({ path: `/visualization/clusters/${cluster.id}` })
}

/**
 * Group servers with the same states together.
 * The state type is determined by using statusIconHelpers
 * @param {Object} cluster
 * @return {Object}
 */
function groupSlaveServersByStateType(cluster) {
  let group = {}
  const master = typy(cluster, 'children[0]').safeObject
  if (master) {
    flattenTree(typy(master, 'children').safeArray).forEach((n) => {
      const groupName = getServerStateType(n.serverData.attributes.state)
      const colorName = getStateColorName(groupName)
      if (!group[groupName]) group[groupName] = { colorName, servers: [] }
      group[groupName].servers.push(n)
    })
  }
  return group
}

function getServerStateType(state) {
  switch (statusIconHelpers[MXS_OBJ_TYPES.SERVERS](state)) {
    case 0:
      return 'error'
    case 1:
      return 'success'
    case 2:
      return 'grayed-out' // for maintenance state
  }
}

function getStateColorName(stateType) {
  switch (stateType) {
    case 'error':
      return 'down'
    case 'success':
      return 'up'
    // grayed-out when server is in maintenance state
    case 'grayed-out':
      return 'maintenance'
  }
}
</script>
<template>
  <VContainer v-if="!$typy(clusterMap).isEmptyObject" fluid>
    <VRow>
      <VCol v-for="cluster in clusterMap" :key="cluster.id" cols="12" md="6" lg="4">
        <VCard hover flat border class="cluster-card" @click="navToCluster(cluster)">
          <div class="d-flex flex-row pa-4">
            <StatusIcon
              v-show="cluster.state"
              size="20"
              class="mr-2"
              :type="MXS_OBJ_TYPES.MONITORS"
              :value="cluster.state"
            />
            <div>
              <GblTooltipActivator
                class="tk-azo-sans-web text-h5 font-weight-medium text-blue-azure d-flex"
                :data="{ txt: String(cluster.id) }"
                :debounce="0"
                activateOnTruncation
              />
              <span class="text-subtitle-2 text-grayed-out"> {{ cluster.module }} </span>
            </div>
          </div>
          <VDivider />
          <VList class="px-7">
            <VListItem :height="48">
              <div class="d-flex align-center justify-space-between">
                <span class="text-subtitle-2 text-uppercase text-navigation">
                  {{ $t('master') }}
                </span>
                <ClusterServerTooltip
                  v-if="$typy(cluster, 'children[0]').isDefined"
                  :servers="[$typy(cluster, 'children[0]').safeObject]"
                >
                  <template #activator="{ props }">
                    <div class="d-inline-flex align-center" v-bind="props">
                      <StatusIcon
                        size="16"
                        class="server-state-icon mr-1"
                        :type="MXS_OBJ_TYPES.SERVERS"
                        :value="
                          $typy(cluster, 'children[0].serverData.attributes.state').safeString
                        "
                      />
                      <GblTooltipActivator
                        class="text-body-2"
                        :data="{
                          txt: String(cluster.children[0].name),
                          activatorID: $helpers.lodash.uniqueId('clusters__cluster-children-name'),
                        }"
                        :debounce="0"
                        activateOnTruncation
                      />
                    </div>
                  </template>
                </ClusterServerTooltip>
              </div>
            </VListItem>
            <VListItem :height="48">
              <div class="d-flex align-center justify-space-between">
                <span class="text-subtitle-2 text-uppercase text-navigation">
                  {{ $t('slaves', 2) }}
                </span>
                <div class="d-flex flex-row align-end">
                  <template
                    v-for="(item, stateType) in groupSlaveServersByStateType(cluster)"
                    :key="stateType"
                  >
                    <ClusterServerTooltip :servers="item.servers">
                      <template #activator="{ props }">
                        <div
                          v-bind="props"
                          class="d-flex align-center text-lowercase chip-wrapper ml-1"
                        >
                          <div
                            class="d-flex align-center fill-height rounded-pill rounded-te-0 rounded-be-0 text-white font-weight-bold px-2"
                            :class="`bg-${stateType}`"
                          >
                            {{ item.servers.length }}
                          </div>
                          <div
                            :class="`bg-${stateType}`"
                            class="d-flex align-center fill-height rounded-pill rounded-ts-0 rounded-bs-0 text-white lighter-color pl-2 pr-3 pb-1"
                          >
                            {{ $t(item.colorName) }}
                          </div>
                        </div>
                      </template>
                    </ClusterServerTooltip>
                  </template>
                </div>
              </div>
            </VListItem>
          </VList>
        </VCard>
      </VCol>
    </VRow>
  </VContainer>
</template>

<style lang="scss" scoped>
.chip-wrapper {
  height: 24px;
  font-size: 0.75rem;
  .lighter-color {
    filter: brightness(115%);
  }
}
</style>
