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
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import OdbcInputs from '@wsComps/OdbcInputs.vue'
import DestConnInputs from '@wkeComps/DataMigration/DestConnInputs.vue'
import MigrationLogs from '@wkeComps/DataMigration/MigrationLogs.vue'
import { MXS_OBJ_TYPES, LOADING_TIME } from '@/constants'
import { QUERY_CONN_BINDING_TYPES } from '@/constants/workspace'

const props = defineProps({
  task: { type: Object, required: true },
  hasConns: { type: Boolean, required: true },
  srcConn: { type: Object, required: true },
  destConn: { type: Object, required: true },
})

const store = useStore()
const { t } = useI18n()
const { delay } = useHelpers()
const fetchObj = useFetchObjData()
const { fetch: fetchOdbcDrivers, data: drivers } = useFetchOdbcDrivers()
const DEST_TARGET_TYPE = MXS_OBJ_TYPES.SERVERS

const formRef = ref(null)
const formValidity = ref(null)
const src = ref({})
const dest = ref({})
const isLoading = ref(false)
const allServers = ref([])

const reqConfig = computed(() => Worksheet.getters('activeRequestConfig'))

onBeforeMount(async () => {
  await fetchOdbcDrivers()
  allServers.value = await fetchObj({
    type: DEST_TARGET_TYPE,
    fields: ['id'],
    reqConfig: reqConfig.value,
  })
})

async function handleOpenConns() {
  isLoading.value = true
  EtlTask.dispatch('pushLog', {
    id: props.task.id,
    log: {
      timestamp: new Date().valueOf(),
      name: t('info.openingConns'),
    },
  })
  if (!props.srcConn.id) await openSrcConn()
  if (!props.destConn.id) await openDestConn()
  if (props.hasConns) {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('success.connected')],
      type: 'success',
    })
    await delay(LOADING_TIME) // UX loading animation
  }
  isLoading.value = false
}

async function openSrcConn() {
  await QueryConn.dispatch('openEtlConn', {
    body: {
      target: 'odbc',
      connection_string: src.value.connection_string,
      timeout: src.value.timeout,
    },
    binding_type: QUERY_CONN_BINDING_TYPES.ETL_SRC,
    etl_task_id: props.task.id,
    taskMeta: { src_type: src.value.type },
    connMeta: { name: src.value.type },
  })
}

async function openDestConn() {
  await QueryConn.dispatch('openEtlConn', {
    body: dest.value,
    binding_type: QUERY_CONN_BINDING_TYPES.ETL_DEST,
    etl_task_id: props.task.id,
    taskMeta: { dest_name: dest.value.target },
    connMeta: { name: dest.value.target },
  })
}

async function next() {
  if (props.hasConns)
    EtlTask.update({
      where: props.task.id,
      data(obj) {
        obj.active_stage_index = obj.active_stage_index + 1
      },
    })
  else {
    await formRef.value.validate()
    if (formValidity.value) await handleOpenConns()
  }
}
</script>

<template>
  <WizardStageCtr>
    <template #body>
      <VForm ref="formRef" v-model="formValidity" class="form-container fill-height">
        <VContainer fluid class="fill-height">
          <VRow class="fill-height">
            <VCol cols="12" md="6" class="fill-height pt-0 mt-n1">
              <OdbcInputs @get-form-data="src = $event" :drivers="drivers" class="pb-1">
                <template #prepend>
                  <VCol cols="12" class="pa-1">
                    <h3 class="text-h3 text-navigation font-weight-light wizard-stage-ctr__title">
                      {{ $t('source') }}
                    </h3>
                  </VCol>
                </template>
              </OdbcInputs>
            </VCol>
            <VCol cols="12" md="6" class="fill-height pt-0 mt-n1">
              <div class="d-flex flex-column fill-height">
                <div class="d-flex">
                  <DestConnInputs
                    :items="allServers"
                    :type="DEST_TARGET_TYPE"
                    @get-form-data="dest = $event"
                  />
                </div>
                <MigrationLogs :task="task" class="mt-4 etl-logs overflow-y-auto" />
              </div>
            </VCol>
          </VRow>
        </VContainer>
      </VForm>
    </template>
    <template #footer>
      <VBtn
        rounded
        variant="flat"
        color="primary"
        class="font-weight-medium px-7 text-capitalize"
        :disabled="!formValidity"
        :loading="isLoading"
        @click="next"
      >
        {{ $t(hasConns ? 'selectObjsToMigrate' : 'connect') }}
      </VBtn>
    </template>
  </WizardStageCtr>
</template>

<style lang="scss" scoped>
.form-container {
  :deep(.etl-logs) {
    display: flex;
    flex: 1;
    min-height: 250px;
    max-height: 400px;
    .log-container {
      background-color: colors.$light-gray !important;
    }
  }
}
</style>
