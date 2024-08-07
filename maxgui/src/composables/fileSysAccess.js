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
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

const { SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP
export function useSaveFile() {
  const store = useStore()
  const logger = useLogger()

  const file_handle_data_map = computed(() => store.state.fileSysAccess.file_handle_data_map)
  const hasFileSystemRWAccess = computed(() => store.getters['fileSysAccess/hasFileSystemRWAccess'])

  /**
   * @private
   */
  function getFileHandleData(id) {
    return file_handle_data_map.value[id] || {}
  }
  /**
   * @private
   * @returns {FileSystemFileHandle} fileHandle
   */
  function getFileHandle(id) {
    return getFileHandleData(id).file_handle || {}
  }

  /**
   * @private
   * @returns {string} FileSystemFileHandle name
   */
  function getFileHandleName(id) {
    return getFileHandle(id).name || ''
  }

  /**
   * @public
   * @param {string} id - query tab id
   * @returns {object} editor object
   */
  function getEditor(id) {
    return TxtEditor.find(id) || DdlEditor.find(id) || {}
  }

  /**
   * @private
   * Verify the user has granted permission to read and write to the file, if
   * permission hasn't been granted, request permission.
   * @param {FileSystemFileHandle} fileHandle File handle to check.
   * @return {promise<boolean>} True if the user has granted read/write permission.
   */
  async function verifyWritePriv(fileHandle) {
    const opts = {}
    opts.writable = true
    opts.mode = 'readwrite' // For Chrome 86 and later...
    // Check if we already have permission, if so, return true.
    if ((await fileHandle.queryPermission(opts)) === 'granted') return true
    // Request permission to the file, if the user grants permission, return true.
    if ((await fileHandle.requestPermission(opts)) === 'granted') return true
    // The user didn't grant permission, return false.
    return false
  }

  /**
   * @private
   * Writes the contents to disk.
   * @param {FileSystemFileHandle} param.fileHandle File handle to write to.
   * @param {string} param.contents Contents to write.
   */
  async function writeFile({ fileHandle, contents }) {
    // Support for Chrome 82 and earlier.
    if (fileHandle.createWriter) {
      // Create a writer (request permission if necessary).
      const writer = await fileHandle.createWriter()
      // Write the full length of the contents
      await writer.write(0, contents)
      // Close the file and write the contents to disk
      await writer.close()
      return
    }
    // For Chrome 83 and later.
    // Create a FileSystemWritableFileStream to write to.
    const writable = await fileHandle.createWritable()
    // Write the contents of the file to the stream.
    await writable.write(contents)
    // Close the file and write the contents to disk.
    await writable.close()
  }

  /**
   * @private
   * Create a handle to a new text file on the local file system.
   * @param {string} suggestedName - suggestedName for the file
   * @returns {Promise<FileSystemFileHandle>} Handle to the new file.
   */
  async function getNewFileHandle(suggestedName) {
    try {
      // For Chrome 86 and later...
      if ('showSaveFilePicker' in window) return await window.showSaveFilePicker({ suggestedName })
      // For Chrome 85 and earlier...
      return await window.chooseFileSystemEntries({ suggestedName, type: 'save-file' })
    } catch (ex) {
      if (!ex.name === 'AbortError') logger.error('An error occurred trying to open the file.')
    }
  }

  /**
   * @private
   * Download the file to user's disk
   * @param {Object} queryTab - queryTab object
   */
  async function saveFileLegacy(queryTab) {
    const editor = getEditor(queryTab.id)
    const a = document.createElement('a')
    // If there is no file_handle, use the current queryTab name
    const fileName = getFileHandleName(queryTab.id) || `${queryTab.name}.sql`
    a.href = `data:application/text;charset=utf-8,${encodeURIComponent(editor.sql)}`
    a.download = fileName
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
    await store.dispatch('fileSysAccess/updateFileHandleDataMap', {
      id: editor.id,
      data: { txt: editor.sql },
    })
  }

  /**
   * @private
   * @param {Object} queryTab - queryTab object
   */
  async function saveFileAs(queryTab) {
    let fileHandleName = queryTab.name
    if (!isFileHandleValid(queryTab.id)) fileHandleName += '.sql'
    const fileHandle = await getNewFileHandle(fileHandleName)
    try {
      const { sql } = getEditor(queryTab.id)
      await writeFile({ fileHandle, contents: sql })
      QueryTab.update({ where: queryTab.id, data: { name: fileHandle.name } })
      await store.dispatch('fileSysAccess/updateFileHandleDataMap', {
        id: queryTab.id,
        data: { file_handle: fileHandle, txt: sql },
      })
    } catch (ex) {
      logger.error('Unable to write file')
    }
  }

  /**
   * @public
   */
  function isFileHandleValid(id) {
    return Boolean(getFileHandleName(id))
  }

  /**
   * @public
   * @param {Object} queryTab - queryTab object
   */
  async function handleSaveFileAs(queryTab) {
    if (hasFileSystemRWAccess.value) await saveFileAs(queryTab)
    else await saveFileLegacy(queryTab)
  }

  /**
   * @public
   * Save new content for the opened file to user's local device
   * @param {promise<object>} queryTab - queryTab object
   */
  async function saveFileToDisk(queryTab) {
    try {
      const fileHandle = getFileHandle(queryTab.id)
      const hasPriv = await verifyWritePriv(fileHandle)
      if (hasPriv) {
        const { sql } = getEditor(queryTab.id)
        await writeFile({ fileHandle, contents: sql })
        await store.dispatch('fileSysAccess/updateFileHandleDataMap', {
          id: queryTab.id,
          data: { file_handle: fileHandle, txt: sql },
        })
      }
    } catch (e) {
      logger.error(e)
    }
  }

  /**
   * @public
   * @param {Object} queryTab - queryTab object
   */
  async function handleSaveFile(queryTab) {
    if (hasFileSystemRWAccess.value && isFileHandleValid(queryTab.id))
      await saveFileToDisk(queryTab)
    else await handleSaveFileAs(queryTab)
  }

  /**
   * Check if the txt based editor has unsaved changes
   * @public
   * @returns {boolean}
   */
  function hasUnsavedChanges(queryTab) {
    const { id, type } = queryTab

    if (type !== SQL_EDITOR && type !== DDL_EDITOR) return false

    const { txt: file_handle_txt = '', file_handle: { name: file_handle_name = '' } = {} } =
      getFileHandleData(id)

    /**
     * If there is no file is opened for DDL_EDITOR, the queryTab is consider to have
     * no changes, as it's not useful to save changes to a file after the SQL has been
     * executed
     */
    if (type === DDL_EDITOR && !file_handle_name) return false

    const { sql = '' } = getEditor(id)

    // no unsaved changes if it's a blank queryTab
    if (!sql && !file_handle_name) return false
    /**
     * If no file is opened, `file_handle_txt` will be empty, which can be used to check if
     * the editor sql has unsaved changes.
     * If a file is opened, `file_handle_txt` will initially match the editor sql,
     * which can be used to check if there have been changes to the file.
     */
    return file_handle_txt !== sql
  }

  return {
    getEditor,
    isFileHandleValid,
    handleSaveFileAs,
    saveFileToDisk,
    handleSaveFile,
    hasUnsavedChanges,
  }
}
