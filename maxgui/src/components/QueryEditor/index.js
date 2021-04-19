/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { format } from 'sql-formatter'
import { languageConfiguration, languageTokens } from './mariadbLang'
import reservedWords from './reservedWords.js'
export default {
    name: 'query-editor',
    props: {
        value: { type: String, required: true },
        tableDist: { type: Array, default: () => [] },
    },

    model: {
        prop: 'value',
        event: 'change',
    },

    data() {
        return {
            completionList: [],
            language: 'mariadb',
        }
    },
    watch: {
        value(v) {
            if (this.editor && v !== this.getEditorValue()) this.setEditorValue(v)
        },
        tableDist: {
            deep: true,
            handler() {
                this.createCompletionList()
            },
        },
    },
    computed: {
        completionListLabels() {
            return this.completionList.map(item => item.label)
        },
    },
    mounted() {
        this.monaco = require('monaco-editor')
        this.createCompletionList()
        this.$nextTick(() => this.initMonaco(this.monaco))
    },

    beforeDestroy() {
        if (this.editor) this.editor.dispose()
    },

    methods: {
        createCompletionList() {
            const keywordList = reservedWords.map(w => ({
                label: w.keyword,
                detail: 'KEYWORD',
                kind: this.monaco.languages.CompletionItemKind.Keyword,
                insertText: w.keyword,
            }))

            const dist = this.$help.lodash.cloneDeep(this.tableDist)
            for (const item of dist) {
                switch (item.type) {
                    case 'table':
                    case 'column':
                    case 'schema':
                        item.kind = this.monaco.languages.CompletionItemKind.Text
                }
            }

            this.completionList = [...dist, ...keywordList]
        },
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
                },
            })

            const options = {
                value: this.codeFormatter(this.value),
                theme: 'mariadb-theme',
                language: this.language,
                automaticLayout: true,
                fontSize: 12,
                lineNumbersMinChars: 2,
                scrollbar: {
                    verticalScrollbarSize: 10,
                    horizontalScrollbarSize: 10,
                },
            }

            this.editor = monaco.editor.create(this.$el, options)

            const scope = this
            //TODO: Move below computes to web worker
            monaco.languages.registerCompletionItemProvider(this.language, {
                provideCompletionItems: function(model, position) {
                    const wordObj = model.getWordUntilPosition(position)
                    const range = {
                        startLineNumber: position.lineNumber,
                        endLineNumber: position.lineNumber,
                        startColumn: wordObj.startColumn,
                        endColumn: wordObj.endColumn,
                    }

                    const match = scope.completionListLabels.find(label =>
                        label.includes(wordObj.word)
                    )

                    const suggestions = match ? scope.createCompleters(range) : []
                    return { suggestions }
                },
            })
            monaco.languages.registerDocumentFormattingEditProvider(this.language, {
                provideDocumentFormattingEdits: function(model) {
                    return [
                        {
                            range: model.getFullModelRange(),
                            text: scope.codeFormatter(model.getValue()),
                        },
                    ]
                },
            })
            this.editor.onDidChangeModelContent(event => {
                const editorValue = this.getEditorValue()
                if (this.value !== editorValue) this.$emit('change', editorValue, event)
            })
        },
        createCompleters(range) {
            return this.completionList.map(item => ({ ...item, range }))
        },
        getEditorValue() {
            return this.editor.getValue()
        },
        setEditorValue(value) {
            if (this.editor) return this.editor.setValue(value)
        },
    },

    render(createElement) {
        return createElement('div')
    },
}
