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
import QueryTabTmp from '@wsModels/QueryTabTmp'
import AlterEditor from '@wsModels/AlterEditor'
import DdlEditor from '@wsModels/DdlEditor'
import SchemaNodeIcon from '@wsComps/SchemaNodeIcon.vue'
import queryConnService from '@wsServices/queryConnService'
import workspaceService from '@wsServices/workspaceService'
import { useSaveFile } from '@/composables/fileSysAccess'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

const props = defineProps({ queryTab: { type: Object, required: true } })
const emit = defineEmits(['delete'])

const { DDL_EDITOR } = QUERY_TAB_TYPE_MAP
const TAB_WIDTH = 162
const BTN_CTR_WIDTH = 24

const { handleSaveFile, isQueryTabUnsaved } = useSaveFile()
const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const {
  lodash: { isEqual },
} = useHelpers()

const confirm_dlg = computed(() => store.state.workspace.confirm_dlg)

const tabId = computed(() => props.queryTab.id)
const tabType = computed(() => props.queryTab.type)
const queryTabTmp = computed(() => QueryTabTmp.find(tabId.value) || {})
const ddlEditor = computed(() => DdlEditor.find(tabId.value) || {})
const isUnsaved = computed(() => isQueryTabUnsaved(tabId.value))
const initialAlterEditorData = computed(
  () => typy(AlterEditor.find(tabId.value), 'data').safeObjectOrEmpty
)
const alterEditorStagingData = computed(
  () => typy(queryTabTmp.value, 'alter_editor_staging_data').safeObjectOrEmpty
)
const hasAlterEditorDataChanged = computed(() => {
  if (typy(alterEditorStagingData.value).isEmptyObject) return false
  return !isEqual(initialAlterEditorData.value, alterEditorStagingData.value)
})
const isLoading = computed(() => workspaceService.getIsLoading(queryTabTmp.value))
const isQueryTabConnBusy = computed(
  () => typy(queryConnService.findQueryTabConn(tabId.value), 'is_busy').safeBoolean
)

function onClickDelete() {
  if (isUnsaved.value || hasAlterEditorDataChanged.value) {
    const i18n_interpolation = {
      keypath: 'confirmations.deleteQueryTab',
      values: [props.queryTab.name],
    }
    let on_save = async () => {
        await handleSaveFile(props.queryTab)
        emit('delete', tabId.value)
      },
      after_cancel = () => emit('delete', tabId.value),
      save_text = 'save',
      cancel_text = 'dontSave'

    if (hasAlterEditorDataChanged.value) {
      i18n_interpolation.keypath = 'confirmations.deleteAlterTab'
      on_save = () => emit('delete', tabId.value)
      after_cancel = () => null
      save_text = 'confirm'
      cancel_text = 'cancel'
    }
    store.commit('workspace/SET_CONFIRM_DLG', {
      ...confirm_dlg.value,
      is_opened: true,
      save_text,
      cancel_text,
      title: t('deleteTab'),
      i18n_interpolation,
      on_save,
      after_cancel,
    })
  } else emit('delete', tabId.value)
}
</script>

<template>
  <VHover>
    <template #default="{ isHovering, props }">
      <div
        :style="{ width: `${TAB_WIDTH}px` }"
        class="fill-height d-flex align-center justify-space-between pl-3 pr-1"
        v-bind="props"
      >
        <div
          class="d-inline-flex align-center"
          :style="{ maxWidth: `calc(100% - ${BTN_CTR_WIDTH}px)` }"
        >
          <SchemaNodeIcon
            v-if="tabType === DDL_EDITOR"
            :type="ddlEditor.type"
            :size="12"
            class="mr-1"
            color="primary"
          />
          <GblTooltipActivator
            :data="{ txt: queryTab.name, nudgeLeft: 36 }"
            activateOnTruncation
            fillHeight
          />
          <span v-if="isUnsaved || hasAlterEditorDataChanged" class="changes-indicator" />
        </div>
        <div
          :style="{ width: `${BTN_CTR_WIDTH}px` }"
          class="d-inline-flex align-center fill-height"
        >
          <VProgressCircular
            v-if="isLoading"
            class="ml-1"
            size="16"
            width="2"
            color="primary"
            indeterminate
          />
          <VBtn
            v-else
            v-show="isHovering"
            class="ml-1"
            variant="text"
            icon
            size="small"
            density="compact"
            :disabled="isQueryTabConnBusy"
            @click.stop.prevent="onClickDelete"
          >
            <VIcon size="8" :color="isQueryTabConnBusy ? '' : 'error'" icon="mxs:close" />
          </VBtn>
        </div>
      </div>
    </template>
  </VHover>
</template>

<style lang="scss" scoped>
.changes-indicator::after {
  content: ' *';
  color: colors.$primary;
  padding-left: 4px;
  font-size: 0.875rem;
  position: relative;
  font-weight: 500;
  font-family: vuetifyVar.$heading-font-family;
}
</style>
