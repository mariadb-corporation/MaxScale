<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import { ETL_STATUS_MAP } from '@/constants/workspace'

const props = defineProps({
  task: { type: Object, required: true },
  hasConns: { type: Boolean, required: true },
})

const disabled = computed(
  () =>
    props.hasConns ||
    props.task.status === ETL_STATUS_MAP.COMPLETE ||
    props.task.status === ETL_STATUS_MAP.RUNNING
)

function next() {
  EtlTask.update({
    where: props.task.id,
    data(obj) {
      obj.active_stage_index = obj.active_stage_index + 1
    },
  })
}
</script>
<template>
  <WizardStageCtr>
    <template #header>
      <VIcon size="48" color="info" class="mr-5 mt-n1" icon="mxs:dataMigration" />
      <h3 class="text-h3 text-navigation font-weight-light wizard-stage-ctr__title">
        {{ $t('dataMigration') }}
      </h3>
    </template>
    <template #body>
      <VContainer fluid class="fill-height">
        <VRow class="fill-height">
          <VCol cols="12" md="6" class="fill-height text-navigation">
            <p class="mb-4">{{ $t('info.etlOverviewInfo') }}</p>
            <a
              target="_blank"
              href="https://mariadb.com/kb/en/mariadb-maxscale-2302-sql-resource/#prepare-etl-operation"
              rel="noopener noreferrer"
              class="anchor-link"
            >
              {{ $t('info.etlDocLinkText') }}
            </a>
            <a
              target="_blank"
              href="https://mariadb.com/kb/en/mariadb-maxscale-2302-limitations-and-known-issues-within-mariadb-maxscale/#etl-limitations"
              rel="noopener noreferrer"
              class="d-block anchor-link"
            >
              {{ $t('limitations') }}
            </a>
          </VCol>
        </VRow>
      </VContainer>
    </template>
    <template #footer>
      <VBtn
        rounded
        variant="flat"
        color="primary"
        class="mt-auto font-weight-medium px-7 text-capitalize"
        :disabled="disabled"
        @click="next"
      >
        {{ $t('setUpConns') }}
      </VBtn>
    </template>
  </WizardStageCtr>
</template>
