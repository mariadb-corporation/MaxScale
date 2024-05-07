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
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import CreateModeInput from '@wkeComps/DataMigration/CreateModeInput.vue'
import MigrationLogs from '@wkeComps/DataMigration/MigrationLogs.vue'
import VirSchemaTree from '@wsComps/VirSchemaTree.vue'
import SchemaNodeIcon from '@wsComps/SchemaNodeIcon.vue'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import etlTaskService from '@wsServices/etlTaskService'
import { NODE_TYPES, NODE_GROUP_TYPES, ETL_CREATE_MODES } from '@/constants/workspace'
import { useDisplay } from 'vuetify/lib/framework.mjs'
import { getChildNodes } from '@/store/queryHelper'

const props = defineProps({ task: { type: Object, required: true } })

const { t } = useI18n()
const { width: windowWidth } = useDisplay()

const selectedObjs = ref([])
const errMsg = ref('')
const warningMsg = ref('')
const isConfirmed = ref(false)
const expandedNodes = ref([])

const reqConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const activeConn = computed(() => QueryConn.getters('activeEtlSrcConn'))
const srcSchemaTree = computed(() => etlTaskService.findSrcSchemaTree(props.task.id))
const createMode = computed(() => etlTaskService.findCreateMode(props.task.id))
const categorizeObjs = computed(() =>
  selectedObjs.value.reduce(
    (obj, o) => {
      const schema = schemaNodeHelper.getSchemaName(o)
      if (o.type === NODE_TYPES.SCHEMA) obj.emptySchemas.push(schema)
      else obj.tables.push({ schema, table: o.name })
      return obj
    },
    { tables: [], emptySchemas: [] }
  )
)
const tables = computed(() => categorizeObjs.value.tables)
const isReplaceMode = computed(() => createMode.value === ETL_CREATE_MODES.REPLACE)
const disabled = computed(() =>
  tables.value.length ? (isReplaceMode.value ? !isConfirmed.value : false) : true
)
const isLarge = computed(() => windowWidth >= 960)

watch(
  selectedObjs,
  (v) => {
    if (v.length) {
      if (tables.value.length) {
        errMsg.value = ''
        warningMsg.value = categorizeObjs.value.emptySchemas.length
          ? t('warnings.ignoredMigrationSchemas')
          : ''
      } else errMsg.value = t('errors.emptyMigrationSchema')
    } else errMsg.value = t('errors.emptyMigrationObj')
    EtlTaskTmp.update({ where: props.task.id, data: { migration_objs: tables.value } })
  },
  { deep: true }
)

onBeforeMount(async () => await etlTaskService.fetchSrcSchemas())

function onTreeChanges(tree) {
  EtlTaskTmp.update({ where: props.task.id, data: { src_schema_tree: tree } })
}

async function loadTables(node) {
  return await getChildNodes({
    connId: activeConn.value.id,
    nodeGroup: schemaNodeHelper.genNodeGroup({ parentNode: node, type: NODE_GROUP_TYPES.TBL_G }),
    nodeAttrs: { onlyIdentifier: true, isLeaf: true },
    config: reqConfig.value,
  })
}

async function next() {
  EtlTask.update({
    where: props.task.id,
    data(obj) {
      obj.active_stage_index = obj.active_stage_index + 1
      obj.is_prepare_etl = true
    },
  })
  await etlTaskService.handleEtlCall({
    id: props.task.id,
    tables: etlTaskService.findMigrationObjs(props.task.id),
  })
}
</script>

<template>
  <WizardStageCtr>
    <template #header>
      <h3
        class="text-h3 text-navigation font-weight-light wizard-stage-ctr__title"
        data-test="stage-header-title"
      >
        {{ $t('selectObjsToMigrate') }}
      </h3>
    </template>
    <template #body>
      <VContainer fluid class="fill-height">
        <VRow class="fill-height">
          <VCol cols="12" md="6" class="fill-height">
            <div class="d-flex flex-column fill-height">
              <CreateModeInput :taskId="task.id" class="mb-2" />
              <VirSchemaTree
                v-model:expandedNodes="expandedNodes"
                v-model:selectedNodes="selectedObjs"
                class="obj-tree fill-height overflow-y-auto border--separator pa-2 rounded"
                :data="srcSchemaTree"
                height="100%"
                :loadChildren="loadTables"
                @on-tree-changes="onTreeChanges"
              >
                <template #label="{ node }">
                  <SchemaNodeIcon :node="node" :size="16" class="mr-1" />
                  <span class="text-truncate d-inline-block node-name">{{ node.name }}</span>
                </template>
              </VirSchemaTree>
            </div>
          </VCol>
          <VCol cols="12" md="6" class="fill-height">
            <MigrationLogs :task="task" class="fill-height" :class="{ 'pt-10': isLarge }" />
          </VCol>
        </VRow>
      </VContainer>
    </template>
    <template #footer>
      <div class="etl-obj-select-stage-footer d-flex flex-column justify-end">
        <p v-if="errMsg" class="text-body-2 mb-4 text-error">{{ errMsg }}</p>
        <p v-else-if="warningMsg" class="text-body-2 mb-4 text-warning">
          {{ warningMsg }}
        </p>
        <VCheckbox
          v-if="isReplaceMode"
          v-model="isConfirmed"
          color="primary"
          class="mb-4"
          hide-details
          data-test="confirm-checkbox"
        >
          <template #label>
            <VTooltip location="top" max-width="340">
              <template #activator="{ props }">
                <div class="d-flex align-center" v-bind="props">
                  <span class="ml-1 text-deep-ocean confirm-label">
                    {{ $t('etlConfirmMigration') }}
                  </span>
                  <VIcon
                    class="ml-1 material-icons-outlined cursor--pointer"
                    size="16"
                    color="warning"
                    icon="mxs:statusWarning"
                  />
                </div>
              </template>
              {{ $t('info.etlConfirm') }}
            </VTooltip>
          </template>
        </VCheckbox>
        <VBtn
          rounded
          variant="flat"
          color="primary"
          class="font-weight-medium px-7 text-capitalize prepare-btn"
          :width="215"
          :disabled="disabled"
          data-test="prepare-btn"
          @click="next"
        >
          {{ $t('prepareMigrationScript') }}
        </VBtn>
      </div>
    </template>
  </WizardStageCtr>
</template>

<style lang="scss" scoped>
.confirm-label {
  font-size: 0.875rem;
}
.obj-tree {
  background-color: colors.$light-gray;
  .node-name {
    font-size: 0.875rem;
  }
}
</style>
