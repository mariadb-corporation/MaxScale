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
import EtlTaskManage from '@wkeComps/DataMigration/EtlTaskManage.vue'
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon.vue'
import etlTaskService from '@wsServices/etlTaskService'
import { ETL_ACTION_MAP, ETL_STATUS_MAP } from '@/constants/workspace'

defineProps({ height: { type: Number, required: true } })

const { CANCEL, DELETE, DISCONNECT, VIEW } = ETL_ACTION_MAP
const ACTION_TYPES = [CANCEL, DELETE, DISCONNECT, VIEW]

const { dateFormat } = useHelpers()
const typy = useTypy()

const activeItemId = ref(null)

const commonHeaderProps = { cellProps: { class: 'pl-3 pr-0' }, headerProps: { class: 'pl-3 pr-0' } }
const HEADERS = [
  {
    title: 'Task Name',
    value: 'name',
    sortable: true,
    autoTruncate: true,
    cellProps: { class: 'cursor--pointer text-anchor', style: { maxWidth: '250px' } },
  },
  { title: 'Status', value: 'status', sortable: true, ...commonHeaderProps },
  { title: 'Created', value: 'created', sortable: true, ...commonHeaderProps },
  { title: 'From->To', value: 'meta', sortable: true, ...commonHeaderProps },
  {
    title: '',
    value: 'action',
    cellProps: { class: 'pl-0 pr-3', align: 'end' },
    headerProps: { class: 'pl-0 pr-3', align: 'end' },
  },
]

const tableRows = computed(() =>
  EtlTask.all().map((t) => ({ ...t, created: dateFormat({ value: t.created }) }))
)

function parseMeta(meta) {
  return {
    from: typy(meta, 'src_type').safeString || 'Unknown',
    to: typy(meta, 'dest_name').safeString || 'Unknown',
  }
}

function view(item) {
  etlTaskService.view(item)
}
</script>

<template>
  <VDataTable
    class="etl-tasks-table"
    :headers="HEADERS"
    :items="tableRows"
    :sort-by="[{ key: 'created', order: 'desc' }]"
    :itemsPerPage="-1"
    fixed-header
    :height="height"
    hide-default-footer
  >
    <template #item="{ item, columns }">
      <VHover>
        <template #default="{ isHovering, props }">
          <tr class="v-data-table__tr" v-bind="props">
            <CustomTblCol
              v-for="(h, i) in columns"
              :key="h.value"
              :value="item[h.value]"
              :name="h.value"
              :autoTruncate="h.autoTruncate"
              v-bind="columns[i].cellProps"
              @click="h.value === 'name' ? view(item) : null"
            >
              <template #[`item.status`]="{ value: statusValue }">
                <div class="d-flex align-center">
                  <EtlStatusIcon
                    :icon="statusValue"
                    :spinning="statusValue === ETL_STATUS_MAP.RUNNING"
                  />
                  {{ statusValue }}
                  <span v-if="statusValue === ETL_STATUS_MAP.RUNNING">...</span>
                </div>
              </template>
              <template #[`item.meta`]="{ value: metaValue }">
                <div class="d-flex">
                  {{ parseMeta(metaValue).from }}
                  <span class="mx-1 dashed-arrow d-inline-flex align-center">
                    <span class="line"></span>
                    <VIcon
                      color="primary"
                      size="12"
                      class="arrow rotate-right"
                      icon="mxs:arrowHead"
                    />
                  </span>
                  {{ parseMeta(metaValue).to }}
                </div>
              </template>
              <template #[`item.action`]>
                <EtlTaskManage
                  :task="item"
                  :types="ACTION_TYPES"
                  @update:modelValue="activeItemId = $event ? item.id : null"
                >
                  <template #activator="{ props }">
                    <VBtn density="comfortable" icon variant="text" v-bind="props">
                      <VIcon
                        v-show="isHovering || activeItemId === item.id"
                        size="18"
                        color="navigation"
                        icon="$mdiDotsHorizontal"
                      />
                    </VBtn>
                  </template>
                </EtlTaskManage>
              </template>
            </CustomTblCol>
          </tr>
        </template>
      </VHover>
    </template>
  </VDataTable>
</template>

<style lang="scss" scoped>
.dashed-arrow {
  .line {
    border-bottom: 2px dashed colors.$primary;
    width: 22px;
  }
}
</style>
