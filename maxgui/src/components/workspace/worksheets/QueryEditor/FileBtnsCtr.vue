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
import QueryTab from '@wsModels/QueryTab'
import TxtEditor from '@wsModels/TxtEditor'
import DdlEditor from '@wsModels/DdlEditor'
import { fileOpen } from 'browser-fs-access'
import { SNACKBAR_TYPE_MAP } from '@/constants'
import { OS_CMD, QUERY_TAB_TYPE_MAP, KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'
import { WS_KEY, WS_EDITOR_KEY } from '@/constants/injectionKeys'
import { useSaveFile } from '@/composables/fileSysAccess'

const props = defineProps({ queryTab: { type: Object, required: true } })

const { CTRL_O, META_O, CTRL_S, META_S, CTRL_SHIFT_S, META_SHIFT_S } = KEYBOARD_SHORTCUT_MAP

const { SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const EDITOR_MODEL_MAP = {
  [SQL_EDITOR]: TxtEditor,
  [DDL_EDITOR]: DdlEditor,
}

const {
  isFileHandleValid,
  hasUnsavedChanges,
  handleSaveFile,
  saveFileToDisk,
  handleSaveFileAs,
  getEditor,
} = useSaveFile()

const confirm_dlg = computed(() => store.state.workspace.confirm_dlg)
const hasFileSystemReadOnlyAccess = computed(
  () => store.getters['fileSysAccess/hasFileSystemReadOnlyAccess']
)
const hasFileSystemRWAccess = computed(() => store.getters['fileSysAccess/hasFileSystemRWAccess'])
const queryTabType = computed(() => props.queryTab.type)
const isSaveFileDisabled = computed(
  () => !hasUnsavedChanges(props.queryTab) || !isFileHandleValid(props.queryTab.id)
)
const isSaveFileAsDisabled = computed(() => !typy(getEditor(props.queryTab.id), 'sql').safeString)

const wsEvtListener = inject(WS_KEY)
const editorEvtListener = inject(WS_EDITOR_KEY)
const uploaderRef = ref(null)

let unwatch_wsEventListener, unwatch_editorEventListener

onActivated(() => {
  unwatch_wsEventListener = watch(wsEvtListener, (v) => shortKeyHandler(v.name))
  unwatch_editorEventListener = watch(editorEvtListener, (v) => shortKeyHandler(v.name))
})

onDeactivated(() => cleanUp())
onBeforeUnmount(() => cleanUp())

function cleanUp() {
  unwatch_wsEventListener()
  unwatch_editorEventListener()
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
    reader.onload = () => {
      resolve(reader.result)
    }
    reader.readAsText(fileHandle)
  })
}

/**
 * @param {FileSystemFileHandle} fileHandle File handle.
 * @returns {promise<string>} returns file content
 */
async function getFileTxt(fileHandle) {
  if (hasFileSystemReadOnlyAccess.value) {
    // get file contents
    const file = await fileHandle.getFile()
    return await file.text()
  }
  return getFileTextLegacy(fileHandle)
}

// legacy upload file changed support
async function onFileLoadChanged(e) {
  const blob = { handle: e.target.files[0] }
  await handleLoadFile(blob)
}

async function openFileLegacy() {
  await uploaderRef.value.click()
}

async function openFile() {
  const blob = await fileOpen({ description: 'Text files' })
  await handleLoadFile(blob)
}

async function handleFileOpen() {
  if (hasFileSystemReadOnlyAccess.value) openFile()
  else openFileLegacy()
}

/**
 * @param {Blob} blob - blob
 */
async function handleLoadFile(blob) {
  if (hasUnsavedChanges(props.queryTab)) {
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

/**
 * @param {Blob} blob - blob
 */
async function loadFileToActiveQueryTab(blob) {
  const blobTxt = await getFileTxt(blob.handle)
  QueryTab.update({ where: props.queryTab.id, data: { name: blob.handle.name } })
  if (!hasFileSystemReadOnlyAccess.value)
    /**
     * clear the uploader file input so that if the user upload the same file,
     * onFileLoadChanged event handler can be triggered again to show the dialog
     */
    uploaderRef.value.value = ''
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

  const editor = EDITOR_MODEL_MAP[queryTabType.value]
  if (editor) editor.update({ where: props.queryTab.id, data: { sql: blobTxt } })
}

function shortKeyHandler(key) {
  switch (key) {
    case CTRL_O:
    case META_O:
      handleFileOpen()
      break
    case CTRL_S:
    case META_S: {
      if (!isSaveFileDisabled.value && hasFileSystemRWAccess.value) saveFileToDisk(props.queryTab)
      break
    }
    case CTRL_SHIFT_S:
    case META_SHIFT_S: {
      if (!isSaveFileAsDisabled.value) handleSaveFileAs(props.queryTab)
      break
    }
  }
}
</script>

<template>
  <div>
    <TooltipBtn
      square
      size="small"
      variant="text"
      type="file"
      color="primary"
      @click="handleFileOpen"
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
    <TooltipBtn
      v-if="hasFileSystemRWAccess"
      square
      size="small"
      variant="text"
      :disabled="isSaveFileDisabled"
      color="primary"
      @click="saveFileToDisk(queryTab)"
    >
      <template #btn-content>
        <VIcon size="20" icon="$mdiContentSaveOutline" />
      </template>
      {{ $t('saveScript') }}
      <br />
      {{ OS_CMD }} + S
    </TooltipBtn>
    <TooltipBtn
      square
      size="small"
      variant="text"
      :disabled="isSaveFileAsDisabled"
      color="primary"
      @click="handleSaveFileAs(queryTab)"
    >
      <template #btn-content>
        <VIcon size="20" icon="$mdiContentSaveEditOutline" />
      </template>
      {{ $t('saveScriptAs') }}
      <br />
      {{ OS_CMD }} + SHIFT + S
    </TooltipBtn>
  </div>
</template>
