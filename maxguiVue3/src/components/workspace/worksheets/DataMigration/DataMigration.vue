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
import OverviewStage from '@wkeComps/DataMigration/OverviewStage.vue'
import ConnsStage from '@wkeComps/DataMigration/ConnsStage.vue'
import ObjSelectStage from '@wkeComps/DataMigration/ObjSelectStage.vue'
import MigrationStage from '@wkeComps/DataMigration/MigrationStage.vue'
import { QUERY_CONN_BINDING_TYPES, ETL_STATUS } from '@/constants/workspace'

const props = defineProps({ taskId: { type: String, required: true } })

const { t } = useI18n()
const typy = useTypy()

const task = computed(() => EtlTask.getters('findRecord')(props.taskId))
const hasEtlRes = computed(() => Boolean(EtlTask.getters('findResTables')(props.taskId).length))
const conns = computed(() => QueryConn.query().where('etl_task_id', props.taskId).get())
const srcConn = computed(
  () => conns.value.find((c) => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_SRC) || {}
)
const destConn = computed(
  () => conns.value.find((c) => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_DEST) || {}
)
const hasConns = computed(() => conns.value.length === 2)
const isPreparingEtl = computed(() => typy(task.value, 'is_prepare_etl').safeBoolean)
const isMigrationDisabled = computed(() =>
  isPreparingEtl.value ? !hasConns.value : !hasEtlRes.value
)
const activeStageIdx = computed({
  get: () => task.value.active_stage_index,
  set: (v) =>
    EtlTask.update({
      where: props.taskId,
      data: { active_stage_index: v },
    }),
})
const stages = computed(() => {
  const { status } = task.value
  const props = { task: task.value }
  return [
    {
      name: t('overview'),
      component: OverviewStage,
      isDisabled: false,
      props: { ...props, hasConns: hasConns.value },
    },
    {
      name: t('connections', 1),
      component: ConnsStage,
      isDisabled: hasConns.value || status === ETL_STATUS.COMPLETE || status === ETL_STATUS.RUNNING,
      props: {
        ...props,
        srcConn: srcConn.value,
        destConn: destConn.value,
        hasConns: hasConns.value,
      },
    },
    {
      name: t('objSelection'),
      component: ObjSelectStage,
      isDisabled:
        !hasConns.value || status === ETL_STATUS.COMPLETE || status === ETL_STATUS.RUNNING,
      props,
    },
    {
      name: t('migration'),
      component: MigrationStage,
      isDisabled: isMigrationDisabled.value,
      props: { ...props, srcConn: srcConn.value },
    },
  ]
})
</script>

<template>
  <div class="d-flex fill-height">
    <VTabs v-model="activeStageIdx" direction="vertical" class="v-tabs--vert" hide-slider>
      <VTab
        v-for="(stage, stageIdx) in stages"
        :value="stageIdx"
        :key="stageIdx"
        :disabled="stage.isDisabled"
        class="my-1"
        selectedClass="v-tab--selected text-blue-azure"
        :height="42"
      >
        <span class="pa-2 tab-name"> {{ stage.name }} </span>
      </VTab>
    </VTabs>
    <VWindow v-model="activeStageIdx" class="w-100 fill-height">
      <VWindowItem
        v-for="(stage, stageIdx) in stages"
        :key="stageIdx"
        :value="stageIdx"
        class="w-100 fill-height"
      >
        <component
          :is="stage.component"
          v-show="stageIdx === activeStageIdx"
          :task="task"
          v-bind="stage.props"
        />
      </VWindowItem>
    </VWindow>
  </div>
</template>
