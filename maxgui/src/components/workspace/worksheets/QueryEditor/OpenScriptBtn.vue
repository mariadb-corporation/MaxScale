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
import QueryTab from '@wsModels/QueryTab'
import TxtEditor from '@wsModels/TxtEditor'
import DdlEditor from '@wsModels/DdlEditor'
import { fileOpen } from 'browser-fs-access'
import { useSaveFile } from '@/composables/fileSysAccess'
import workspace from '@/composables/workspace'
import { SNACKBAR_TYPE_MAP } from '@/constants'
import { QUERY_TAB_TYPE_MAP, OS_CMD, KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'

const props = defineProps({
  queryTab: { type: Object, required: true },
  hasUnsavedChanges: { type: Boolean, required: true },
  hasFileSystemReadOnlyAccess: { type: Boolean, required: true },
})

const { SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP
const { CTRL_O, META_O } = KEYBOARD_SHORTCUT_MAP

const EDITOR_MODEL_MAP = {
  [SQL_EDITOR]: TxtEditor,
  [DDL_EDITOR]: DdlEditor,
}

const store = useStore()
const logger = useLogger()
const { t } = useI18n()
const { handleSaveFile } = useSaveFile()
workspace.useShortKeyListener({ handler: shortKeyHandler })

const uploaderRef = ref(null)

const confirm_dlg = computed(() => store.state.workspace.confirm_dlg)

function clearUploadedFileFallback() {
  uploaderRef.value.value = ''
}

async function onFileLoadChanged(e) {
  const file = e.target.files[0]
  if (file) await handleLoadFile({ handle: file })
  clearUploadedFileFallback()
}

async function openFile() {
  try {
    const blob = await fileOpen({ description: 'Text files' })
    await handleLoadFile(blob)
  } catch (e) {
    const err = JSON.stringify(e)
    if (err.includes('The user aborted a request.')) logger.info(e)
    else logger.error(e)
  }
}

async function openFileFallback() {
  await uploaderRef.value.click()
}

/**
 * Legacy support for reading uploaded file
 * @param {FileSystemFileHandle} fileHandle File handle
 * @returns {String} returns file content
 */
function getFileTextLegacy(fileHandle) {
  const reader = new FileReader()
  return new Promise((resolve, reject) => {
    reader.onerror = () => {
      reader.abort()
      reject(new DOMException('Problem parsing input file.'))
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: [reader.error],
        type: SNACKBAR_TYPE_MAP.ERROR,
      })
    }
    reader.onload = () => resolve(reader.result)
    reader.readAsText(fileHandle)
  })
}

/**
 * @param {FileSystemFileHandle} fileHandle File handle.
 * @returns {promise<string>} returns file content
 */
async function getFileTxt(fileHandle) {
  if (props.hasFileSystemReadOnlyAccess) {
    // get file contents
    const file = await fileHandle.getFile()
    return await file.text()
  }
  return getFileTextLegacy(fileHandle)
}

/**
 * @param {Blob} blob - blob
 */
async function loadFileToActiveQueryTab(blob) {
  const blobTxt = await getFileTxt(blob.handle)
  QueryTab.update({ where: props.queryTab.id, data: { name: blob.handle.name } })
  // once file is loaded, store it
  await store.dispatch('fileSysAccess/updateFileHandleDataMap', {
    id: props.queryTab.id,
    data: {
      file_handle: blob.handle,
      /* store its txt so it can be retrieved
       * because the permission to read the file is withdrawn
       * when the browser is refreshed or closed
       */
      txt: blobTxt,
    },
  })

  const editor = EDITOR_MODEL_MAP[props.queryTab.type]
  if (editor) editor.update({ where: props.queryTab.id, data: { sql: blobTxt } })
}

/**
 * @param {Blob} blob - blob
 */
async function handleLoadFile(blob) {
  if (props.hasUnsavedChanges) {
    store.commit('workspace/SET_CONFIRM_DLG', {
      ...confirm_dlg.value,
      is_opened: true,
      title: t('openScript'),
      i18n_interpolation: {
        keypath: 'confirmations.openScript',
        values: [props.queryTab.name, blob.handle.name],
      },
      on_save: async () => {
        await handleSaveFile(props.queryTab)
        await loadFileToActiveQueryTab(blob)
      },
      after_cancel: async () => {
        await loadFileToActiveQueryTab(blob)
        store.commit('workspace/SET_CONFIRM_DLG', { ...confirm_dlg.value, is_opened: false })
      },
    })
  } else await loadFileToActiveQueryTab(blob)
}

async function shortKeyHandler(key) {
  if (key === CTRL_O || key === META_O) await handleOpenFile()
}

async function handleOpenFile() {
  if (props.hasFileSystemReadOnlyAccess) await openFile()
  else openFileFallback()
}
</script>

<template>
  <TooltipBtn
    square
    size="small"
    variant="text"
    type="file"
    color="primary"
    @click="handleOpenFile()"
  >
    <template #btn-content>
      <VIcon
        size="18"
        :icon="hasFileSystemReadOnlyAccess ? '$mdiFileOutline' : '$mdiFileUploadOutline'"
      />
      <input ref="uploaderRef" class="d-none" type="file" @input="onFileLoadChanged" />
    </template>
    {{ $t('openScript') }}
    <br />
    {{ OS_CMD }} + O
  </TooltipBtn>
</template>
