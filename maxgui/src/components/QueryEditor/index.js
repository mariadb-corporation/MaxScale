/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { languageConfiguration, languageTokens } from './mariadbLang'
import './customStyle.css'
import { mapState } from 'vuex'
export default {
    name: 'query-editor',
    props: {
        value: { type: String, required: true },
        cmplList: { type: Array, default: () => [] },
        readOnly: { type: Boolean, default: false },
        options: { type: Object, default: () => {} },
        isKeptAlive: { type: Boolean, default: false },
        skipRegCompleters: { type: Boolean, default: false },
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
        ...mapState({ SQL_NODE_TYPES: state => state.app_config.SQL_NODE_TYPES }),
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
            const { SCHEMA, TABLE, SP, TRIGGER, COL } = this.SQL_NODE_TYPES
            for (const item of dist) {
                switch (item.type) {
                    case SCHEMA:
                    case TABLE:
                    case SP:
                    case COL:
                    case TRIGGER:
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
        const monaco = require('monaco-editor/esm/vs/editor/editor.api.js')
        this.monaco = monaco
    },
    mounted() {
        this.initMonaco(this.monaco)
    },
    beforeDestroy() {
        this.handleDisposeCompletionProvider()
        if (this.editor) this.editor.dispose()
    },
    activated() {
        if (this.isKeptAlive && !this.readOnly && !this.skipRegCompleters)
            this.regCompleters(this.monaco)
    },
    deactivated() {
        if (this.isKeptAlive && !this.readOnly && !this.skipRegCompleters)
            this.handleDisposeCompletionProvider()
    },
    methods: {
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

            this.editor = monaco.editor.create(this.$el, {
                value: this.$help.formatSQL(this.value),
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
            })
            if (!this.readOnly) {
                if (!this.isKeptAlive && !this.skipRegCompleters) this.regCompleters(monaco)
                this.regDocFormattingProvider(monaco)
                this.addWatchers(this.editor)
                this.addCustomCmds(monaco)
            }
        },
        regDocFormattingProvider(monaco) {
            const scope = this
            monaco.languages.registerDocumentFormattingEditProvider(this.language, {
                provideDocumentFormattingEdits: model => [
                    {
                        range: model.getFullModelRange(),
                        text: scope.$help.formatSQL(model.getValue()),
                    },
                ],
            })
        },
        /**
         * Should be called once https://github.com/microsoft/monaco-editor/issues/1957
         * @param {Object} monaco
         */
        regCompleters(monaco) {
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

                        const suggestions = match
                            ? scope.completionItems.map(item => ({ ...item, range }))
                            : []
                        return { suggestions }
                    },
                }
            )
        },
        addWatchers(editor) {
            // Editor watchers
            editor.onDidChangeModelContent(event => {
                const editorValue = this.getEditorValue()
                if (this.value !== editorValue) this.$emit('change', editorValue, event)
            })
            editor.onDidChangeCursorSelection(event => {
                this.$emit('on-selection', this.getSelectedTxt(event.selection))
            })
        },
        addCustomCmds(monaco) {
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
                {
                    label: this.$t('saveStatementsToFavorite', { quantity: '' }),
                    keybindings: [monaco.KeyMod.CtrlCmd | monaco.KeyCode.KEY_S],
                    run: () => this.$emit('onCtrlS'),
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
        handleDisposeCompletionProvider() {
            if (this.completionProvider) this.completionProvider.dispose()
        },
        getEditorValue() {
            return this.editor.getValue()
        },
        setEditorValue(value) {
            if (this.editor) return this.editor.setValue(value)
        },
        getSelectedTxt(selection) {
            return this.editor.getModel().getValueInRange(selection)
        },
        /**
         * @public
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
    },

    render(createElement) {
        return createElement('div')
    },
}
