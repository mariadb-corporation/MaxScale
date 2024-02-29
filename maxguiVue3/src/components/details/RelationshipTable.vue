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
import SelDlg from '@/components/details/SelDlg.vue'

const props = defineProps({
  type: { type: String, required: true },
  data: { type: Array, required: true },
  removable: { type: Boolean, default: false },
  addable: { type: Boolean, default: false },
  customAddableItems: { type: Array },
  getRelationshipData: { type: Function },
})
const emit = defineEmits(['confirm-update', 'click-add-listener'])

const { t } = useI18n()
const store = useStore()
const loading = useLoading()
const {
  lodash: { cloneDeep, xorWith },
} = useHelpers()

let headers = ref([
  {
    title: t(props.type, 1),
    value: 'id',
    autoTruncate: true,
    cellProps: { class: 'pa-0' },
    customRender: {
      renderer: 'AnchorLink',
      objType: props.type,
      props: { class: 'px-6' },
    },
  },
  {
    title: 'Status',
    value: 'state',
    cellProps: { align: 'center' },
    headerProps: { align: 'center' },
    customRender: { renderer: 'StatusIcon', objType: props.type },
  },
])
let dialogTitle = ref('')
let targetItems = ref([])
let deleteDialogType = ref('delete')
let addableItems = ref([])
let isConfDlgOpened = ref(false)
let isSelDlgOpened = ref(false)

const search_keyword = computed(() => store.state.search_keyword)
const isAdmin = computed(() => store.getters['users/isAdmin'])
const items = computed(() =>
  props.type === 'filters' ? cloneDeep(props.data).forEach((row, i) => (row.index = i)) : props.data
)
const addBtnText = computed(
  () =>
    `${t('addEntity', {
      entityName: t(props.type, props.type === 'listeners' ? 1 : 2),
    })}`
)

onMounted(() => updateHeaders())

const actionHeader = {
  title: '',
  value: 'action',
  cellProps: { class: 'pa-0', style: { maxWidth: '32px' } },
  headerProps: { class: 'pa-0' },
}

function updateHeaders() {
  switch (props.type) {
    case 'filters':
      //TODO: Add back the drag&drop to reorder filters
      headers.value = [
        {
          title: '',
          value: 'index',
          width: '1px',
          headerProps: { class: 'px-2' },
        },
        { title: 'Filter', value: 'id' },
      ]
      if (props.removable) headers.value.push(actionHeader)
      break
    case 'servers':
    case 'services':
      if (props.removable) headers.value.push(actionHeader)
      break
  }
}

function onDelete(item) {
  targetItems.value = [item]
  deleteDialogType.value = 'unlink'
  dialogTitle.value = `${t('unlink')} ${t(props.type, 1)}`
  isConfDlgOpened.value = true
}

async function confirmDelete() {
  emit('confirm-update', {
    type: props.type,
    data: items.value.reduce((arr, row) => {
      if (targetItems.value.some((item) => item.id !== row.id))
        arr.push({ id: row.id, type: row.type })
      return arr
    }, []),
  })
}

async function getAllEntities() {
  if (props.customAddableItems) {
    await props.getRelationshipData({ type: props.type })
    addableItems.value = props.customAddableItems
  } else {
    const all = await props.getRelationshipData({ type: props.type })
    const availableEntities = xorWith(all, items.value, (a, b) => a.id === b.id)
    addableItems.value = formRelationshipData(availableEntities)
  }
}

function onAdd() {
  dialogTitle.value = `${t(`addEntity`, {
    entityName: t(props.type, 2),
  })}`

  if (props.type !== 'listeners') isSelDlgOpened.value = true
  else emit('click-add-listener')
}

/**
 * @param {Array} arr - array of object, each object must have id and type attributes
 * @returns {Array} - returns valid relationship array data
 */
function formRelationshipData(arr) {
  return arr.map((item) => ({ id: item.id, type: item.type }))
}

async function confirmAdd() {
  emit('confirm-update', {
    type: props.type,
    data: [...formRelationshipData(items.value), ...formRelationshipData(targetItems.value)],
  })
}
</script>

<template>
  <CollapsibleCtr :title="`${$t(type, 2)}`" :titleInfo="items.length">
    <template #header-right>
      <VBtn
        v-if="isAdmin && addable"
        color="primary"
        variant="text"
        size="x-small"
        class="text-capitalize"
        @click="onAdd"
      >
        + {{ addBtnText }}
      </VBtn>
    </template>
    <VDataTable
      class="relationship-tbl"
      :search="search_keyword"
      :headers="headers"
      :items="items"
      :noDataText="$t('noEntity', { entityName: $t(type, 2) })"
      :loading="loading"
    >
      <template #item="{ item, columns }">
        <tr class="v-data-table__tr">
          <CustomTblCol
            v-for="(h, i) in columns"
            :key="h.value"
            :value="item[h.value]"
            :name="h.value"
            :search="search_keyword"
            :autoTruncate="h.autoTruncate"
            :class="{
              'text-no-wrap': $typy(h.customRender, 'renderer').safeString === 'StatusIcon',
            }"
            v-bind="columns[i].cellProps"
          >
            <template v-if="h.customRender" #[`item.${h.value}`]="{ value, highlighter }">
              <CustomCellRenderer
                :value="value"
                :componentName="h.customRender.renderer"
                :objType="h.customRender.objType"
                :mixTypes="$typy(h.customRender, 'mixTypes').safeBoolean"
                :highlighter="highlighter"
                statusIconWithoutLabel
                v-bind="h.customRender.props"
              />
            </template>
            <template #[`item.action`]>
              <div
                class="del-btn"
                :class="{ 'del-btn--visible': $helpers.lodash.isEqual(targetItems[0], item) }"
              >
                <TooltipBtn
                  density="comfortable"
                  icon
                  variant="text"
                  size="small"
                  color="error"
                  @click="onDelete(item)"
                >
                  <template #btn-content>
                    <VIcon size="18" icon="mxs:unlink" :style="{ transition: 'none' }" />
                  </template>
                  {{ `${$t('unlink')} ${$t(type, 1)}` }}
                </TooltipBtn>
              </div>
            </template>
          </CustomTblCol>
        </tr>
      </template>
      <template #bottom />
    </VDataTable>
    <ConfirmDlg
      v-if="removable"
      v-model="isConfDlgOpened"
      :title="dialogTitle"
      :saveText="deleteDialogType"
      :type="deleteDialogType"
      :item="$typy(targetItems, '[0]').safeObjectOrEmpty"
      :onSave="confirmDelete"
    />
    <SelDlg
      v-if="addable"
      v-model="isSelDlgOpened"
      :title="dialogTitle"
      saveText="add"
      :onSave="confirmAdd"
      :type="type"
      multiple
      :items="addableItems"
      @selected-items="targetItems = $event"
      @on-open="getAllEntities"
    />
  </CollapsibleCtr>
</template>

<style lang="scss" scoped>
.relationship-tbl {
  tbody tr {
    .del-btn {
      visibility: hidden;
      &--visible {
        visibility: visible;
      }
    }
  }
  tbody tr:hover {
    .del-btn {
      visibility: visible;
    }
  }
}
</style>
