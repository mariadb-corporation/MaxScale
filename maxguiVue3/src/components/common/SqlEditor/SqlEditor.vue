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
import { CMPL_SNIPPET_KIND, NODE_TYPES } from '@/constants/workspace'
import { formatSQL } from '@/utils/queryUtils'
import monaco, {
  LANGUAGE,
  builtInCmplItems,
  tabFocusModeKey,
} from '@/components/common/SqlEditor/customMonaco.js'

const props = defineProps({
  modelValue: { type: String, required: true },
  completionItems: { type: Array, default: () => [] },
  readOnly: { type: Boolean, default: false },
  options: { type: Object, default: () => {} },
  isKeptAlive: { type: Boolean, default: false },
  skipRegCompleters: { type: Boolean, default: false },
  isTabMoveFocus: { type: Boolean, default: false },
  whiteBg: { type: Boolean, default: false },
})
const emit = defineEmits(['update:modelValue', 'update:isTabMoveFocus', 'on-selection', 'shortkey'])

const { t } = useI18n()
const {
  lodash: { uniqueId },
} = useHelpers()

let editorInstance, completionProvider

let wrapperRef = ref(null)

const additionalCmplItems = computed(() => {
  const dist = JSON.parse(JSON.stringify(props.completionItems))
  const nodeTypes = Object.values(NODE_TYPES)
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
  () => updateTabFocusMode()
)

onMounted(() => initMonaco())

onBeforeUnmount(() => {
  handleDisposeCompletionProvider()
  if (editorInstance) editorInstance.dispose()
})

onActivated(() => {
  if (props.isKeptAlive && !props.readOnly && !props.skipRegCompleters) regCompleters()
})
onDeactivated(() => {
  if (props.isKeptAlive && !props.readOnly && !props.skipRegCompleters)
    handleDisposeCompletionProvider()
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
  /**
   * the monaco editor hasn't provided a way to persist the changes. So if
   * props.isTabMoveFocus is true and the value of tabFocusModeKey(from the editor) is false,
   * trigger action to toggle the mode
   */
  if (props.isTabMoveFocus && !editorInstance.getOption(tabFocusModeKey))
    editorInstance.trigger('', 'editor.action.toggleTabFocusMode')

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

/**
 * If isTabMoveFocus is changed elsewhere, not by interacting with editor,
 * it should be updated
 */
function updateTabFocusMode() {
  if (props.isTabMoveFocus !== editorInstance.getOption(tabFocusModeKey))
    editorInstance.trigger('', 'editor.action.toggleTabFocusMode')
}

function regDocFormattingProvider() {
  monaco.languages.registerDocumentFormattingEditProvider(LANGUAGE, {
    provideDocumentFormattingEdits: (model) => [
      {
        range: model.getFullModelRange(),
        text: formatSQL(model.getValue()),
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
  editorInstance.onDidChangeConfiguration(() => {
    const tabFocusMode = editorInstance.getOption(tabFocusModeKey)
    if (tabFocusMode !== props.isTabMoveFocus) emit('update:isTabMoveFocus', tabFocusMode)
  })
}

function addCustomCmds() {
  // Add custom commands to palette list
  // should emit shortcut keys that are defined in QUERY_SHORTCUT_KEYS
  const actionDescriptors = [
    {
      label: t('runStatements', { quantity: t('all') }),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.Enter],
      run: () => emit('shortkey', 'ctrl-shift-enter'),
    },
    {
      label: t('runStatements', { quantity: t('selected') }),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter],
      run: () => emit('shortkey', 'ctrl-enter'),
    },
    {
      label: t('createQuerySnippet'),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyD],
      run: () => emit('shortkey', 'ctrl-d'),
    },
    {
      label: t('openScript'),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyO],
      run: () => emit('shortkey', 'ctrl-o'),
    },
    {
      label: t('saveScript'),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS],
      run: () => emit('shortkey', 'ctrl-s'),
    },
    {
      label: t('saveScriptAs'),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.KeyS],
      run: () => emit('shortkey', 'ctrl-shift-s'),
    },
    {
      label: t('stopStatements'),
      keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.KeyC],
      run: () => emit('shortkey', 'ctrl-shift-c'),
    },
  ]
  for (const item of actionDescriptors) {
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
