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
import QueryTab from '@wsModels/QueryTab'
import TxtEditor from '@wsModels/TxtEditor'
import { fileOpen } from 'browser-fs-access'
import { WS_PROVIDER_KEY, EDITOR_PROVIDER_KEY, OS_KEY } from '@/constants/workspace'

const props = defineProps({ queryTab: { type: Object, required: true } })

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const { isFileHandleValid, isQueryTabUnsaved, handleSaveFile, saveFileToDisk, handleSaveFileAs } =
  useSaveFile()

const confirm_dlg = computed(() => store.state.mxsWorkspace.confirm_dlg)
const hasFileSystemReadOnlyAccess = computed(
  () => store.getters['fileSysAccess/hasFileSystemReadOnlyAccess']
)
const hasFileSystemRWAccess = computed(() => store.getters['fileSysAccess/hasFileSystemRWAccess'])
const isSaveFileDisabled = computed(
  () => !isQueryTabUnsaved(props.queryTab.id) || !isFileHandleValid(props.queryTab.id)
)
const isSaveFileAsDisabled = computed(
  () => !typy(TxtEditor.find(props.queryTab.id), 'query_txt').safeString
)

let wsKeypress = inject(WS_PROVIDER_KEY)
let editorKeypress = inject(EDITOR_PROVIDER_KEY)
let uploaderRef = ref(null)

let unwatch_wsKeypress, unwatch_editorKeypress

onActivated(() => {
  unwatch_wsKeypress = watch(wsKeypress, (v) => shortKeyHandler(v.key))
  unwatch_editorKeypress = watch(editorKeypress, (v) => shortKeyHandler(v.key))
})

onDeactivated(() => cleanUp())
onBeforeUnmount(() => cleanUp())

function cleanUp() {
  unwatch_wsKeypress()
  unwatch_editorKeypress()
}

/**
 * Legacy support for reading uploaded file
 * @param {<FileSystemFileHandle>} fileHandle File handle
 * @returns {String} returns file content
 */
function getFileTextLegacy(fileHandle) {
  const reader = new FileReader()
  return new Promise((resolve, reject) => {
    reader.onerror = () => {
      reader.abort()
      reject(new DOMException('Problem parsing input file.'))
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [reader.error], type: 'error' })
    }
    reader.onload = () => {
      resolve(reader.result)
    }
    reader.readAsText(fileHandle)
  })
}

/**
 * @param {<FileSystemFileHandle>} fileHandle File handle.
 * @returns {String} returns file content
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
  if (isQueryTabUnsaved(props.queryTab.id)) {
    store.commit('mxsWorkspace/SET_CONFIRM_DLG', {
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
        store.commit('mxsWorkspace/SET_CONFIRM_DLG', { ...confirm_dlg.value, is_opened: false })
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
  TxtEditor.update({ where: props.queryTab.id, data: { query_txt: blobTxt } })
}

function shortKeyHandler(key) {
  switch (key) {
    case 'ctrl-o':
    case 'mac-cmd-o':
      handleFileOpen()
      break
    case 'ctrl-s':
    case 'mac-cmd-s': {
      if (!isSaveFileDisabled.value && hasFileSystemRWAccess.value) saveFileToDisk(props.queryTab)
      break
    }
    case 'ctrl-shift-s':
    case 'mac-cmd-shift-s': {
      if (!isSaveFileAsDisabled.value) handleSaveFileAs(props.queryTab)
      break
    }
  }
}
</script>

<template>
  <div>
    <TooltipBtn
      class="toolbar-square-btn load-sql-btn"
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
      {{ OS_KEY }} + O
    </TooltipBtn>
    <TooltipBtn
      v-if="hasFileSystemRWAccess"
      class="toolbar-square-btn save-sql-btn"
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
      {{ OS_KEY }} + S
    </TooltipBtn>
    <TooltipBtn
      class="toolbar-square-btn save-sql-btn"
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
      {{ OS_KEY }} + SHIFT + S
    </TooltipBtn>
  </div>
</template>
