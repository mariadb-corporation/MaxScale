const metadata = require('monaco-editor/esm/metadata')
const ignoreFeats = [
    '!anchorSelect',
    '!bracketMatching',
    '!caretOperations',
    '!codeAction',
    '!codelens',
    '!colorPicker',
    '!cursorUndo',
    '!gotoError',
    '!gotoLine',
    '!gotoSymbol',
    '!hover',
    '!iPadShowKeyboard',
    '!inPlaceReplace',
    '!indentation',
    '!inlayHints',
    '!inspectTokens',
    '!linesOperations',
    '!linkedEditing',
    '!links',
    '!parameterHints',
    '!quickHelp',
    '!quickOutline',
    '!referenceSearch',
    '!rename',
    '!smartSelect',
    '!snippets',
    '!toggleHighContrast',
    '!unusualLineTerminators',
    '!viewportSemanticTokens',
    '!wordHighlighter',
    '!wordOperations',
    '!wordPartOperations',
]
const features = metadata.features.reduce((acc, f) => {
    if (!ignoreFeats.includes(f.label)) acc.push(f.label)
    return acc
}, [])
module.exports = {
    languages: ['mariadb'],
    // https://github.com/microsoft/monaco-editor/tree/main/webpack-plugin
    features,
}
