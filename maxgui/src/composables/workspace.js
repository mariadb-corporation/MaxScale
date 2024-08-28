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
import { t as typy } from 'typy'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import queryConnService from '@wsServices/queryConnService'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import { WS_KEY, WS_EDITOR_KEY } from '@/constants/injectionKeys'

/**
 * @param {object} data - proxy object
 */
function useCommonResSetAttrs(data) {
  const isLoading = computed(() => typy(data.value, 'is_loading').safeBoolean)
  const startTime = computed(() => typy(data.value, 'start_time').safeNumber)
  const execTime = computed(() => {
    if (isLoading.value) return -1
    const execution_time = typy(data.value, 'data.attributes.execution_time').safeNumber
    if (execution_time) return parseFloat(execution_time.toFixed(4))
    return 0
  })
  const endTime = computed(() => typy(data.value, 'end_time').safeNumber)
  return {
    isLoading,
    startTime,
    execTime,
    endTime,
  }
}

function useCompletionItems({ queryEditorId, queryTabId }) {
  const store = useStore()
  const queryEditorTmp = computed(() => QueryEditorTmp.find(queryEditorId) || {})
  const queryTabConn = computed(() => queryConnService.findQueryTabConn(queryTabId))
  const queryTabTmp = computed(() => QueryTabTmp.find(queryTabId) || {})
  const activeSchema = computed(() => typy(queryTabConn.value, 'active_db').safeString)
  const snippetCompletionItems = computed(
    () => store.getters['prefAndStorage/snippetCompletionItems']
  )
  const identifier_auto_completion = computed(
    () => store.state.prefAndStorage.identifier_auto_completion
  )
  const schemaTree = computed(() => {
    const tree = typy(queryEditorTmp.value, 'db_tree').safeArray
    if (identifier_auto_completion.value && activeSchema.value)
      return tree.filter((n) => n.qualified_name !== activeSchema.value)
    return tree
  })
  const schemaTreeCompletionItems = computed(() =>
    schemaNodeHelper.genNodeCompletionItems(schemaTree.value)
  )
  const activeSchemaIdentifierCompletionItems = computed(
    () => typy(queryTabTmp.value, 'schema_identifier_names_completion_items').safeArray
  )
  const completionItems = computed(() => [
    ...schemaTreeCompletionItems.value,
    ...activeSchemaIdentifierCompletionItems.value,
    ...snippetCompletionItems.value,
  ])
  return completionItems
}

function useSqlEditorDragDrop(editorRef) {
  const { getAppEle } = useHelpers()
  let mouseDropDOM = null,
    mouseDropWidget = null

  function placeToEditor(text) {
    editorRef.value.insertAtCursor({ text })
  }

  function handleGenMouseDropWidget(dropTarget) {
    /**
     *  Setting text cursor to all elements as a fallback method for firefox
     *  as monaco editor will fail to get dropTarget position in firefox
     *  So only add mouseDropWidget when user agent is not firefox
     */
    if (navigator.userAgent.includes('Firefox')) {
      getAppEle().classList.add(dropTarget ? 'cursor--text--all' : 'cursor--grab--all')
    } else {
      const { getEditorInstance, monaco } = editorRef.value
      const editor = getEditorInstance()
      getAppEle().classList.remove('cursor--grab--all')
      if (dropTarget) {
        const preference = monaco.editor.ContentWidgetPositionPreference.EXACT
        if (!mouseDropDOM) {
          mouseDropDOM = document.createElement('div')
          mouseDropDOM.style.pointerEvents = 'none'
          mouseDropDOM.style.borderLeft = '2px solid #424f62'
          mouseDropDOM.innerHTML = '&nbsp;'
        }
        mouseDropWidget = {
          mouseDropDOM: null,
          getId: () => 'drag',
          getDomNode: () => mouseDropDOM,
          getPosition: () => ({
            position: dropTarget.position,
            preference: [preference, preference],
          }),
        }
        //remove the prev cursor widget first then add
        editor.removeContentWidget(mouseDropWidget)
        editor.addContentWidget(mouseDropWidget)
      } else if (mouseDropWidget) editor.removeContentWidget(mouseDropWidget)
    }
  }

  function draggingTxt(e) {
    const { getEditorInstance } = editorRef.value
    // build mouseDropWidget
    const dropTarget = getEditorInstance().getTargetAtClientPoint(e.clientX, e.clientY)
    handleGenMouseDropWidget(dropTarget)
  }

  function dropTxtToEditor(e) {
    if (e.target.textContent) {
      const { getEditorInstance, monaco, insertAtCursor } = editorRef.value
      const editor = getEditorInstance()
      const dropTarget = editor.getTargetAtClientPoint(e.clientX, e.clientY)

      if (dropTarget) {
        const dropPos = dropTarget.position
        // create range
        const range = new monaco.Range(
          dropPos.lineNumber,
          dropPos.column,
          dropPos.lineNumber,
          dropPos.column
        )
        const text = e.target.textContent.trim()
        insertAtCursor({ text, range })
        if (mouseDropWidget) editor.removeContentWidget(mouseDropWidget)
      }
      getAppEle().className = ''
    }
  }

  return { placeToEditor, draggingTxt, dropTxtToEditor }
}

export function useShortKeyHandler(handler) {
  const wsEvtListener = inject(WS_KEY)
  const editorEvtListener = inject(WS_EDITOR_KEY)
  let unwatch_wsEventListener, unwatch_editorEventListener

  onActivated(() => {
    unwatch_wsEventListener = watch(wsEvtListener, (v) => handler(v.name))
    unwatch_editorEventListener = watch(editorEvtListener, (v) => handler(v.name))
  })

  onDeactivated(() => cleanUp())
  onBeforeUnmount(() => cleanUp())

  function cleanUp() {
    unwatch_wsEventListener()
    unwatch_editorEventListener()
  }
}

export default {
  useCommonResSetAttrs,
  useCompletionItems,
  useSqlEditorDragDrop,
  useShortKeyHandler,
}
