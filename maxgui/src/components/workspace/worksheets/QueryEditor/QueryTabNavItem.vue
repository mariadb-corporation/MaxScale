<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryTabTmp from '@wsModels/QueryTabTmp'
import AlterEditor from '@wsModels/AlterEditor'
import queryConnService from '@wsServices/queryConnService'
import { useSaveFile } from '@/composables/fileSysAccess'

const props = defineProps({ queryTab: { type: Object, required: true } })
const emit = defineEmits(['delete'])

const { handleSaveFile, isQueryTabUnsaved } = useSaveFile()
const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const {
  lodash: { isEqual },
} = useHelpers()

const confirm_dlg = computed(() => store.state.workspace.confirm_dlg)

const tabId = computed(() => props.queryTab.id)
const queryTabTmp = computed(() => QueryTabTmp.find(tabId.value) || {})
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
const isLoadingQueryResult = computed(
  () => typy(queryTabTmp.value, 'query_results.is_loading').safeBoolean
)
const isQueryTabConnBusy = computed(
  () => typy(queryConnService.findQueryTabConn(tabId.value), 'is_busy').safeBoolean
)

function onClickDelete() {
  if (isUnsaved.value || hasAlterEditorDataChanged.value) {
    let i18n_interpolation = {
        keypath: 'confirmations.deleteQueryTab',
        values: [props.queryTab.name],
      },
      on_save = async () => {
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
      <span
        :style="{ width: '162px' }"
        class="fill-height d-flex align-center justify-space-between px-3"
        v-bind="props"
      >
        <span class="d-inline-flex align-center">
          <GblTooltipActivator
            :data="{ txt: queryTab.name, nudgeLeft: 36 }"
            activateOnTruncation
            :maxWidth="112"
            fillHeight
          />
          <span v-if="isUnsaved || hasAlterEditorDataChanged" class="changes-indicator" />
        </span>
        <VProgressCircular
          v-if="isLoadingQueryResult"
          class="ml-2"
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
      </span>
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
