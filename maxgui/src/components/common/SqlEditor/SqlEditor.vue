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
import { CMPL_SNIPPET_KIND, NODE_TYPE_MAP } from '@/constants/workspace'
import { formatSQL } from '@/utils/queryUtils'
import monaco, {
  LANGUAGE,
  builtInCmplItems,
  tabFocusModeKey,
} from '@/components/common/SqlEditor/customMonaco.js'
import sqlSplitter, { findCustomDelimiter } from '@/utils/sqlSplitter'

const props = defineProps({
  modelValue: { type: String, required: true },
  completionItems: { type: Array, default: () => [] },
  readOnly: { type: Boolean, default: false },
  options: { type: Object, default: () => {} },
  isKeptAlive: { type: Boolean, default: false },
  skipRegCompleters: { type: Boolean, default: false },
  isTabMoveFocus: { type: Boolean, default: false },
  whiteBg: { type: Boolean, default: false },
  customActions: { type: Array, default: () => [] },
  supportCustomDelimiter: { type: Boolean, default: false },
})
const emit = defineEmits(['update:modelValue', 'on-selection', 'toggle-tab-focus-mode'])

const typy = useTypy()
const {
  lodash: { uniqueId },
} = useHelpers()

let editorInstance, completionProvider

const wrapperRef = ref(null)

const additionalCmplItems = computed(() => {
  const dist = JSON.parse(JSON.stringify(props.completionItems))
  const nodeTypes = Object.values(NODE_TYPE_MAP)
  for (const item of dist) {
    if (nodeTypes.includes(item.type)) item.kind = monaco.languages.CompletionItemKind.Text
    else if (item.type === CMPL_SNIPPET_KIND)
      item.kind = monaco.languages.CompletionItemKind.Snippet
  }
  return dist
})

const allCompletionItems = computed(() => [...additionalCmplItems.value, ...builtInCmplItems])

watch(
  () => props.modelValue,
  (v) => {
    if (editorInstance && v !== getEditorValue()) setEditorValue(v)
  }
)
watch(
  () => props.isTabMoveFocus,
  () => {
    if (!props.isKeptAlive) triggerToggleTabFocusMode()
  }
)

onMounted(() => initMonaco())

let unwatch_isTabMoveFocus
onBeforeUnmount(() => {
  handleDisposeCompletionProvider()
  if (editorInstance) editorInstance.dispose()
  typy(unwatch_isTabMoveFocus).safeFunction()
})
onActivated(() => {
  if (props.isKeptAlive) {
    unwatch_isTabMoveFocus = watch(
      () => props.isTabMoveFocus,
      () => triggerToggleTabFocusMode()
    )
    if (!props.readOnly && !props.skipRegCompleters) regCompleters()
  }
})
onDeactivated(() => {
  if (props.isKeptAlive && !props.readOnly && !props.skipRegCompleters)
    handleDisposeCompletionProvider()
  typy(unwatch_isTabMoveFocus).safeFunction()
})

function initMonaco() {
  editorInstance = monaco.editor.create(wrapperRef.value, {
    value: props.modelValue,
    readOnly: props.readOnly,
    theme: 'mariadb-theme',
    language: LANGUAGE,
    automaticLayout: true,
    fontSize: 12,
    lineNumbersMinChars: 3,
    lineHeight: 18,
    minimap: { enabled: false },
    scrollbar: { verticalScrollbarSize: 10, horizontalScrollbarSize: 10 },
    overviewRulerLanes: 0,
    ...props.options,
  })

  triggerToggleTabFocusMode()

  if (!props.readOnly) {
    if (!props.isKeptAlive && !props.skipRegCompleters) regCompleters()
    regDocFormattingProvider()
    addWatchers()
    addCustomCmds()
  }
  // Show completion item detail property by default
  const { widget } = editorInstance.getContribution('editor.contrib.suggestController')
  if (widget) {
    const suggestWidget = widget.value
    if (suggestWidget && suggestWidget._setDetailsVisible) suggestWidget._setDetailsVisible(true)
  }
}

function isTabFocusModeChanged() {
  return props.isTabMoveFocus !== editorInstance.getOption(tabFocusModeKey)
}

function triggerToggleTabFocusMode() {
  /**
   * Monaco editor hasn't provided a way to persist the changes.
   * So before toggling the mode, check if the new value for props.isTabMoveFocus
   * is different compared to tabFocusModeKey.
   */
  if (isTabFocusModeChanged()) editorInstance.trigger('', 'editor.action.toggleTabFocusMode')
}

function addTwoLineBreaks(part) {
  if (part.endsWith('\n\n')) return ''
  if (part.endsWith('\n')) return '\n'
  return '\n\n'
}

function format(sql) {
  if (props.supportCustomDelimiter) {
    let result = ''
    const [e, statements] = sqlSplitter(sql)
    if (!e) {
      let pos = 0
      statements.forEach((stmt, i) => {
        // Ignore formatting the part that was extracted by splitQuery function
        const extractedPart = sql.substring(pos, stmt.trimStart.position)
        result += extractedPart

        // Add 2 line breaks after the DELIMITER command or default delimiter
        if (findCustomDelimiter(extractedPart)) result += addTwoLineBreaks(extractedPart)
        else if (extractedPart.trim() === ';') result += addTwoLineBreaks(extractedPart)

        result += formatSQL(stmt.text, { linesBetweenQueries: 0 })

        pos = stmt.trimEnd.position

        // add the last extracted part
        if (i === statements.length - 1) {
          const lastExtractedPart = sql.substring(pos)
          result += lastExtractedPart
          if (findCustomDelimiter(lastExtractedPart)) result += addTwoLineBreaks(lastExtractedPart)
        }
      })
      return result
    }
    return sql
  }
  return formatSQL(sql)
}

function regDocFormattingProvider() {
  monaco.languages.registerDocumentFormattingEditProvider(LANGUAGE, {
    provideDocumentFormattingEdits: (model) => [
      {
        range: model.getFullModelRange(),
        text: format(model.getValue()),
      },
    ],
  })
}

/**
 * Should be called once https://github.com/microsoft/monaco-editor/issues/1957
 */
function regCompleters() {
  completionProvider = monaco.languages.registerCompletionItemProvider(LANGUAGE, {
    provideCompletionItems: (model, position) => {
      const wordObj = model.getWordUntilPosition(position)
      const range = {
        startLineNumber: position.lineNumber,
        endLineNumber: position.lineNumber,
        startColumn: wordObj.startColumn,
        endColumn: wordObj.endColumn,
      }
      const suggestions = allCompletionItems.value.map((item) => ({ ...item, range }))
      return { suggestions }
    },
  })
}

function addWatchers() {
  // Editor watchers
  editorInstance.onDidChangeModelContent((event) => {
    const editorValue = getEditorValue()
    if (props.modelValue !== editorValue) emit('update:modelValue', editorValue, event)
  })
  let prevSelectedTxt = ''
  editorInstance.onDidChangeCursorSelection((event) => {
    const tmp = getSelectedTxt(event.selection)
    if (tmp !== prevSelectedTxt) {
      emit('on-selection', tmp)
      prevSelectedTxt = tmp
    }
  })
  // add watcher for built-in "Toggle Tab Key Moves Focus" option in monaco-editor
  editorInstance.onDidChangeConfiguration(() => {
    if (isTabFocusModeChanged()) emit('toggle-tab-focus-mode')
  })
}

function addCustomCmds() {
  for (const item of props.customActions) {
    editorInstance.addAction({
      id: uniqueId('monaco_action_id_'),
      precondition: null,
      keybindingContext: null,
      contextMenuGroupId: 'navigation',
      contextMenuOrder: 1.5,
      ...item,
    })
  }
}

function handleDisposeCompletionProvider() {
  if (completionProvider) completionProvider.dispose()
}

function getEditorValue() {
  return editorInstance.getValue()
}

function setEditorValue(value) {
  if (editorInstance) return editorInstance.setValue(value)
}

function getSelectedTxt(selection) {
  return editorInstance.getModel().getValueInRange(selection)
}

/**
 * @public
 * @param {Object} editOptions - IIdentifiedSingleEditOperation
 */
function insertAtCursor(editOptions) {
  if (editorInstance) {
    // default position is at current cursor position
    const p = editorInstance.getPosition()
    editorInstance.executeEdits('', [
      {
        range: new Range(p.lineNumber, p.column, p.lineNumber, p.column),
        ...editOptions,
      },
    ])
  }
}

function getEditorInstance() {
  return editorInstance
}

defineExpose({ insertAtCursor, getEditorInstance, monaco })
</script>

<template>
  <div
    ref="wrapperRef"
    class="sql-editor"
    :class="{ 'sql-editor--readonly': readOnly, 'bg--white': whiteBg }"
  />
</template>

<style lang="scss" scoped>
.sql-editor {
  &--readonly:not(.bg--white) {
    :deep(.overflow-guard) {
      .monaco-editor,
      .monaco-editor-background {
        background-color: colors.$separator;
      }
      .margin {
        background-color: colors.$separator;
      }
      .view-line {
        background-color: colors.$separator;
      }
    }
  }
  :deep(.monaco-editor) {
    .slider {
      border-radius: 4px;
      background: colors.$grayed-out;
    }
  }
}
</style>
