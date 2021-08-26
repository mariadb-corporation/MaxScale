/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { format } from 'sql-formatter'
import { languageConfiguration, languageTokens } from './mariadbLang'
/**
 * Monaco features
 * https://github.com/microsoft/monaco-editor-webpack-plugin/blob/main/src/features.ts
 * Only import necessary features to reduce bundle size,
 * others are commented out and will be uncommented when requested
 */
import 'monaco-editor/esm/vs/editor/browser/controller/coreCommands.js'
/*
import 'monaco-editor/esm/vs/editor/browser/widget/codeEditorWidget.js';
import 'monaco-editor/esm/vs/editor/browser/widget/diffEditorWidget.js';
import 'monaco-editor/esm/vs/editor/browser/widget/diffNavigator.js';
import 'monaco-editor/esm/vs/editor/contrib/anchorSelect/anchorSelect.js';
import 'monaco-editor/esm/vs/editor/contrib/bracketMatching/bracketMatching.js';
import 'monaco-editor/esm/vs/editor/contrib/caretOperations/caretOperations.js';
import 'monaco-editor/esm/vs/editor/contrib/caretOperations/transpose.js';
import 'monaco-editor/esm/vs/editor/contrib/codeAction/codeActionContributions.js';
import 'monaco-editor/esm/vs/editor/contrib/codelens/codelensController.js';
import 'monaco-editor/esm/vs/editor/contrib/colorPicker/colorContributions.js';
import 'monaco-editor/esm/vs/editor/contrib/cursorUndo/cursorUndo.js';
import 'monaco-editor/esm/vs/editor/contrib/gotoError/gotoError.js';
import 'monaco-editor/esm/vs/editor/contrib/gotoSymbol/goToCommands.js';
import 'monaco-editor/esm/vs/editor/contrib/gotoSymbol/link/goToDefinitionAtPosition.js';
import 'monaco-editor/esm/vs/editor/contrib/hover/hover.js';
import 'monaco-editor/esm/vs/editor/contrib/inPlaceReplace/inPlaceReplace.js';
import 'monaco-editor/esm/vs/editor/contrib/indentation/indentation.js';
import 'monaco-editor/esm/vs/editor/contrib/inlineHints/inlineHintsController.js';
import 'monaco-editor/esm/vs/editor/contrib/linesOperations/linesOperations.js';
import 'monaco-editor/esm/vs/editor/contrib/linkedEditing/linkedEditing.js';
import 'monaco-editor/esm/vs/editor/contrib/links/links.js';
import 'monaco-editor/esm/vs/editor/contrib/parameterHints/parameterHints.js';
import 'monaco-editor/esm/vs/editor/contrib/rename/rename.js';
import 'monaco-editor/esm/vs/editor/contrib/smartSelect/smartSelect.js';
import 'monaco-editor/esm/vs/editor/contrib/snippet/snippetController2.js';
import 'monaco-editor/esm/vs/editor/contrib/toggleTabFocusMode/toggleTabFocusMode.js';
import 'monaco-editor/esm/vs/editor/contrib/unusualLineTerminators/unusualLineTerminators.js';
import 'monaco-editor/esm/vs/editor/contrib/viewportSemanticTokens/viewportSemanticTokens.js';
import 'monaco-editor/esm/vs/editor/contrib/wordHighlighter/wordHighlighter.js';
import 'monaco-editor/esm/vs/editor/contrib/wordOperations/wordOperations.js';
import 'monaco-editor/esm/vs/editor/contrib/wordPartOperations/wordPartOperations.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/accessibilityHelp/accessibilityHelp.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/iPadShowKeyboard/iPadShowKeyboard.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/inspectTokens/inspectTokens.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/quickAccess/standaloneGotoLineQuickAccess.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/quickAccess/standaloneGotoSymbolQuickAccess.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/quickAccess/standaloneHelpQuickAccess.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/referenceSearch/standaloneReferenceSearch.js';
import 'monaco-editor/esm/vs/editor/standalone/browser/toggleHighContrast/toggleHighContrast.js';
*/

import * as monaco from 'monaco-editor/esm/vs/editor/editor.api.js'
import './customStyle.css'
export default {
    name: 'query-editor',
    props: {
        value: { type: String, required: true },
        cmplList: { type: Array, default: () => [] },
        readOnly: { type: Boolean, default: false },
        options: { type: Object, default: () => {} },
        isKeptAlive: { type: Boolean, default: false },
    },

    model: {
        prop: 'value',
        event: 'change',
    },

    data() {
        return {
            language: 'mariadb',
        }
    },
    watch: {
        value(v) {
            if (this.editor && v !== this.getEditorValue()) this.setEditorValue(v)
        },
    },
    computed: {
        builtInCmplItems() {
            const keywordCmplItems = languageTokens.keywords.map(s => ({
                label: s,
                detail: 'KEYWORD',
                kind: this.monaco.languages.CompletionItemKind.Keyword,
                insertText: s,
            }))
            const builtinFunctionCmplItems = languageTokens.builtinFunctions.map(s => ({
                label: s,
                detail: 'FUNCTION',
                kind: this.monaco.languages.CompletionItemKind.Function,
                insertText: s,
            }))
            return [...keywordCmplItems, ...builtinFunctionCmplItems]
        },
        custCmplList() {
            const dist = this.$help.lodash.cloneDeep(this.cmplList)
            for (const item of dist) {
                switch (item.type) {
                    case 'Table':
                    case 'Column':
                    case 'Schema':
                    case 'Stored Procedure':
                    case 'Trigger':
                        item.kind = this.monaco.languages.CompletionItemKind.Text
                }
            }
            return dist
        },
        completionItems() {
            return [...this.custCmplList, ...this.builtInCmplItems]
        },
        completionItemLabels() {
            return this.completionItems.map(item => item.label)
        },
    },
    beforeCreate() {
        this.monaco = monaco
    },
    mounted() {
        this.initMonaco(this.monaco)
    },
    beforeDestroy() {
        this.completionProvider.dispose()
        if (this.editor) this.editor.dispose()
    },
    activated() {
        if (this.isKeptAlive) this.registerCompleters()
    },
    deactivated() {
        if (this.isKeptAlive) this.completionProvider.dispose()
    },
    methods: {
        codeFormatter(v) {
            return format(v, {
                language: this.language,
                indent: '   ',
                uppercase: true,
                linesBetweenQueries: 1,
            })
        },
        initMonaco(monaco) {
            // Register a new language
            monaco.languages.register({ id: this.language })
            monaco.languages.setLanguageConfiguration(this.language, languageConfiguration)

            // Register a tokens provider for the language
            monaco.languages.setMonarchTokensProvider(this.language, languageTokens)

            monaco.editor.defineTheme('mariadb-theme', {
                base: 'vs',
                inherit: true, // false to completely replace the builtin rules
                rules: [
                    { token: '', foreground: '#333333', background: '#ffffff' },

                    { token: 'comment.mariadb', foreground: '#60a0b0' },
                    { token: 'comment.quote.mariadb', foreground: '#60a0b0' },

                    { token: 'number.mariadb', foreground: '#40a070' },
                    { token: 'delimiter.mariadb', foreground: '#000000' },

                    { token: 'string.mariadb', foreground: '#4070a0' },
                    { token: 'string.double.mariadb', foreground: '#4070a0' },

                    { token: 'keyword.mariadb', foreground: '#007020', fontStyle: 'bold' },
                    { token: 'operator.mariadb', foreground: '#666666' },
                    { token: 'predefined.mariadb', foreground: '#FF00FF' },
                ],
                //https://microsoft.github.io/monaco-editor/playground.html#customizing-the-appearence-exposed-colors
                colors: {
                    foreground: '#525a65',
                    errorForeground: '#eb5757',
                    'editor.foreground': '#525a65', // code color
                    'editor.background': '#ffffff', // editor background color
                    'editorCursor.foreground': '#424f62', // cursor color
                    'editorLineNumber.foreground': '#a3bac0', // line number color:
                    'editorRuler.foreground': '#e7eef1', // Color of the editor rulers.
                    'editorError.foreground': '#eb5757',
                    'editorError.border': '#eb5757',
                    'editorWarning.foreground': '#f59d34',
                    'editorWarning.border': '#f59d34',
                    'editorSuggestWidget.background': '#ffffff',
                    'editorSuggestWidget.border': '#e7eef1',
                    'editorSuggestWidget.foreground': '#525a65',
                    'editorSuggestWidget.selectedBackground': '#f2fcff',
                    'editorSuggestWidget.highlightForeground': '#0e9bc0',
                    'editor.lineHighlightBackground': '#ffffff',
                },
            })

            const options = {
                value: this.codeFormatter(this.value),
                theme: 'mariadb-theme',
                language: this.language,
                automaticLayout: true,
                fontSize: 12,
                lineNumbersMinChars: 2,
                minimap: { enabled: false },
                scrollbar: {
                    verticalScrollbarSize: 10,
                    horizontalScrollbarSize: 10,
                },
                overviewRulerLanes: 0,
                hideCursorInOverviewRuler: true,
                overviewRulerBorder: false,
                readOnly: this.readOnly,
                ...this.options,
            }

            this.editor = monaco.editor.create(this.$el, options)
            //Lazy loading modules after editor is created
            monaco.editor.onDidCreateEditor(() => {
                import('monaco-editor/esm/vs/editor/contrib/clipboard/clipboard.js')
                import('monaco-editor/esm/vs/editor/contrib/comment/comment.js')
                import('monaco-editor/esm/vs/editor/contrib/contextmenu/contextmenu.js')
                import('monaco-editor/esm/vs/editor/contrib/dnd/dnd.js')
                import('monaco-editor/esm/vs/editor/contrib/documentSymbols/documentSymbols.js')
                import('monaco-editor/esm/vs/editor/contrib/find/findController.js')
                import('monaco-editor/esm/vs/editor/contrib/folding/folding.js')
                import('monaco-editor/esm/vs/editor/contrib/fontZoom/fontZoom.js')
                import('monaco-editor/esm/vs/editor/contrib/format/formatActions.js')
                import('monaco-editor/esm/vs/editor/contrib/multicursor/multicursor.js')
                import('monaco-editor/esm/vs/editor/contrib/suggest/suggestController.js')
                import(
                    // eslint-disable-next-line vue/max-len
                    'monaco-editor/esm/vs/editor/standalone/browser/quickAccess/standaloneCommandsQuickAccess.js'
                )
            })

            if (!this.isKeptAlive) this.registerCompleters()
            const scope = this
            monaco.languages.registerDocumentFormattingEditProvider(this.language, {
                provideDocumentFormattingEdits: model => [
                    {
                        range: model.getFullModelRange(),
                        text: scope.codeFormatter(model.getValue()),
                    },
                ],
            })

            // Editor watchers
            this.editor.onDidChangeModelContent(event => {
                const editorValue = this.getEditorValue()
                if (this.value !== editorValue) this.$emit('change', editorValue, event)
            })
            this.editor.onDidChangeCursorSelection(event => {
                this.$emit('on-selection', this.getSelectedTxt(event.selection))
            })

            // Add custom commands to palette list
            const actionDescriptors = [
                {
                    label: this.$t('runStatements', { quantity: this.$t('all') }),
                    keybindings: [
                        monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.Enter,
                    ],
                    run: () => this.$emit('onCtrlShiftEnter'),
                },
                {
                    label: this.$t('runStatements', { quantity: this.$t('selected') }),
                    keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter],
                    run: () => this.$emit('onCtrlEnter'),
                },
            ]
            for (const item of actionDescriptors) {
                this.editor.addAction({
                    id: this.$help.lodash.uniqueId('monaco_action_id_'),
                    precondition: null,
                    keybindingContext: null,
                    contextMenuGroupId: 'navigation',
                    contextMenuOrder: 1.5,
                    ...item,
                })
            }
        },
        registerCompleters() {
            const scope = this
            this.completionProvider = monaco.languages.registerCompletionItemProvider(
                this.language,
                {
                    provideCompletionItems: function(model, position) {
                        const wordObj = model.getWordUntilPosition(position)
                        const range = {
                            startLineNumber: position.lineNumber,
                            endLineNumber: position.lineNumber,
                            startColumn: wordObj.startColumn,
                            endColumn: wordObj.endColumn,
                        }

                        const match = scope.completionItemLabels.find(label =>
                            label.includes(wordObj.word)
                        )

                        const suggestions = match ? scope.createCompleters(range) : []
                        return { suggestions }
                    },
                }
            )
        },
        createCompleters(range) {
            return this.completionItems.map(item => ({ ...item, range }))
        },
        getEditorValue() {
            return this.editor.getValue()
        },
        setEditorValue(value) {
            if (this.editor) return this.editor.setValue(value)
        },
        /**
         * @param {Object} editOptions - IIdentifiedSingleEditOperation
         */
        insertAtCursor(editOptions) {
            if (this.editor) {
                // default position is at current cursor position
                const p = this.editor.getPosition()
                this.editor.executeEdits('', [
                    {
                        range: new this.monaco.Range(
                            p.lineNumber,
                            p.column,
                            p.lineNumber,
                            p.column
                        ),
                        ...editOptions,
                    },
                ])
            }
        },
        getSelectedTxt(selection) {
            return this.editor.getModel().getValueInRange(selection)
        },
    },

    render(createElement) {
        return createElement('div')
    },
}
