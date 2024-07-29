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
import EtlTask from '@wsModels/EtlTask'
import MigrationTblScript from '@wkeComps/DataMigration/MigrationTblScript.vue'
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon.vue'
import MigrationManage from '@wkeComps/DataMigration/MigrationManage.vue'
import MigrationLogs from '@wkeComps/DataMigration/MigrationLogs.vue'
import etlTaskService from '@wsServices/etlTaskService'
import { ETL_STATUS_MAP, ETL_API_STAGE_MAP } from '@/constants/workspace'

const props = defineProps({
  task: { type: Object, required: true },
  srcConn: { type: Object, required: true },
})

const {
  lodash: { cloneDeep },
} = useHelpers()
const typy = useTypy()
const { t } = useI18n()
const { sortBy, toggleSortBy } = useSortBy({ key: '', isDesc: false })

const stagingScript = ref([])
const activeItem = ref(null)

const taskId = computed(() => props.task.id)
const status = computed(() => typy(props.task, 'status').safeString)
const etlRes = computed(() => etlTaskService.findEtlRes(taskId.value))
const etlResTable = computed(() => etlTaskService.findResTables(taskId.value))

const items = computed(() => {
  const data = cloneDeep(etlResTable.value)
  if (sortBy.value.key) data.sort(compareFn)
  return data
})

const migrationStage = computed(() => etlTaskService.findResStage(taskId.value))
const isRunning = computed(() => status.value === ETL_STATUS_MAP.RUNNING)
const isInErrState = computed(() => status.value === ETL_STATUS_MAP.ERROR)
const queryId = computed(() => typy(props.task, 'meta.async_query_id').safeString)
const isPrepareEtl = computed(() => typy(props.task, 'is_prepare_etl').safeBoolean)
const hasErrAtCreationStage = computed(
  () => isInErrState.value && migrationStage.value === ETL_API_STAGE_MAP.CREATE
)
const generalErr = computed(() => typy(etlRes.value, 'error').safeString)
const tableHeaders = computed(() =>
  isPrepareEtl.value
    ? [
        { title: 'SCHEMA', value: 'schema', autoTruncate: true, headerProps: { width: '50%' } },
        { title: 'TABLE', value: 'table', autoTruncate: true, headerProps: { width: '50%' } },
      ]
    : [
        { title: 'OBJECT', value: 'obj', autoTruncate: true, headerProps: { width: '60%' } },
        { title: 'RESULT', value: 'result', autoTruncate: true, headerProps: { width: '40%' } },
      ]
)
const isOutputMsgShown = computed(() => {
  if (isPrepareEtl.value) {
    if (!typy(etlRes.value, 'ok').isDefined) return false
    return !typy(etlRes.value, 'ok').safeBoolean
  }
  return true
})
const prepareScriptInfo = computed(() =>
  t(isInErrState.value ? 'errors.failedToPrepareMigrationScript' : 'info.migrationScriptInfo')
)
watch(
  queryId,
  async (v) => {
    if (v && props.srcConn.id) await etlTaskService.getEtlCallRes(props.task.id)
  },
  { immediate: true }
)

function objMigrationStatus(item) {
  let icon = ETL_STATUS_MAP.RUNNING,
    isSpinning = isRunning.value,
    txt = `${item.rows || 0} rows migrated`
  if (item.error) {
    icon = ETL_STATUS_MAP.ERROR
    isSpinning = false
    txt = t('error')
  } else if (item.execution_time) {
    icon = ETL_STATUS_MAP.COMPLETE
    isSpinning = false
    if (hasErrAtCreationStage.value) {
      icon = { value: 'mxs:alertWarning', semanticColor: 'warning' }
      txt = t('warnings.objCreation')
    }
  }
  return { icon, isSpinning, txt }
}

function getCellValue(item, key) {
  switch (key) {
    case 'obj':
      return `\`${item.schema}\`.\`${item.table}\``
    case 'result':
      return objMigrationStatus(item).txt
    case 'schema':
      return item.schema
    case 'table':
      return item.table
    default:
      return ''
  }
}

function compareFn(a, b) {
  const aStr = getCellValue(a, sortBy.value.key)
  const bStr = getCellValue(b, sortBy.value.key)
  return sortBy.value.isDesc ? bStr.localeCompare(aStr) : aStr.localeCompare(bStr)
}

async function onRestart(id) {
  await etlTaskService.handleEtlCall({ id, tables: stagingScript.value })
}

async function start() {
  const id = props.task.id
  EtlTask.update({
    where: id,
    data(obj) {
      obj.is_prepare_etl = false
    },
  })
  await etlTaskService.handleEtlCall({ id, tables: stagingScript.value })
}
</script>

<template>
  <WizardStageCtr>
    <template #header>
      <div class="etl-migration-stage__header">
        <div class="d-flex align-center">
          <h3
            class="text-h3 text-navigation font-weight-light wizard-stage-ctr__title"
            data-test="stage-header-title"
          >
            {{ $t('migration') }}
          </h3>
          <MigrationManage v-if="!isPrepareEtl" :task="task" @on-restart="onRestart" />
        </div>
        <div class="my-4 text-body-2">
          <EtlStatusIcon
            :icon="status"
            :spinning="isRunning"
            class="mb-1"
            data-test="header-status-icon"
          />
          <span
            v-if="isPrepareEtl && !isRunning"
            class="text-navigation"
            data-test="prepare-script-info"
          >
            {{ prepareScriptInfo }}
          </span>
          <span v-if="generalErr" class="text-navigation" data-test="general-err">
            {{ generalErr }}
          </span>
          <span
            v-else-if="hasErrAtCreationStage"
            class="text-navigation"
            data-test="creation-stage-err"
          >
            {{ $t(`errors.etl_create_stage`) }}
          </span>
          <span v-else-if="!isPrepareEtl" class="text-navigation" data-test="fallback-msg">
            {{ $t(status.toLowerCase()) }}
            <template v-if="isRunning">...</template>
          </span>
        </div>
      </div>
    </template>
    <template #body>
      <VContainer fluid class="fill-height">
        <VProgressLinear
          v-if="isPrepareEtl && isRunning"
          indeterminate
          color="primary"
          class="align-self-start"
        />
        <MigrationLogs v-else-if="!items.length && isInErrState" :task="task" class="fill-height" />
        <MigrationTblScript
          v-else
          class="migration-tbl"
          :task="task"
          :data="items"
          :headers="tableHeaders"
          @get-activeRow="activeItem = $event"
          @get-staging-data="stagingScript = $event"
        >
          <template #headers="{ columns }">
            <tr>
              <template v-for="column in columns" :key="column.value">
                <CustomTblHeader
                  :column="column"
                  :sortBy="sortBy"
                  @click="toggleSortBy(column.value)"
                />
              </template>
            </tr>
          </template>
          <template #[`item.obj`]="{ item }">
            <GblTooltipActivator
              :data="{ txt: String(getCellValue(item, 'obj')) }"
              activateOnTruncation
            />
          </template>
          <template #[`item.result`]="{ item }">
            <div class="d-flex align-center flex-row">
              <EtlStatusIcon
                :icon="objMigrationStatus(item).icon"
                :spinning="objMigrationStatus(item).isSpinning"
              />
              <GblTooltipActivator
                :data="{ txt: objMigrationStatus(item).txt }"
                activateOnTruncation
              />
            </div>
          </template>
        </MigrationTblScript>
      </VContainer>
    </template>
    <template v-if="!isRunning" #footer>
      <div
        class="etl-migration-stage__footer d-flex flex-column flex-grow-1"
        :class="{ 'etl-migration-stage__footer--with-log': isOutputMsgShown }"
        data-test="stage-footer"
      >
        <template v-if="isOutputMsgShown">
          <h6 class="text-navigation text-h6">
            {{ $t('outputMsgs') }}
          </h6>
          <code
            class="fill-height overflow-y-auto rounded border--separator pa-4 output-msg-ctr"
            data-test="output-msg-ctr"
          >
            <template v-if="activeItem">
              {{
                activeItem.error ||
                (hasErrAtCreationStage
                  ? $t('warnings.objCreation')
                  : objMigrationStatus(activeItem).txt)
              }}
              <br />
              <template v-if="$typy(activeItem, 'execution_time').isDefined">
                {{ $t('exeTime') }}:
                {{ $t('seconds', { n: activeItem.execution_time }) }}
              </template>
            </template>
          </code>
        </template>
        <VBtn
          v-if="isPrepareEtl && !isOutputMsgShown"
          rounded
          variant="flat"
          color="primary"
          class="mt-auto font-weight-medium px-7 text-capitalize start-btn"
          :width="135"
          :disabled="Boolean(generalErr) || isRunning || isInErrState"
          data-test="start-migration-btn"
          @click="start"
        >
          {{ $t('startMigration') }}
        </VBtn>
      </div>
    </template>
  </WizardStageCtr>
</template>

<style lang="scss" scoped>
.etl-migration-stage__footer {
  &--with-log {
    min-height: 150px;
    max-height: 200px;
    .output-msg-ctr {
      font-size: 0.75rem;
      flex: 1 1 auto;
    }
  }
}
</style>
