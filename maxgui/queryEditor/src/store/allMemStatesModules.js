import queryHelper from '@queryEditorSrc/store/queryHelper'

const modules = ['queryConn', 'queryResult', 'schemaSidebar']

export default modules.reduce((acc, m) => {
    acc[m] = queryHelper.memStateCreator(m)
    return acc
}, {})
