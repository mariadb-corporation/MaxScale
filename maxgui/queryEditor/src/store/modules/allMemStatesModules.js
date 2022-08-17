import queryHelper from './queryHelper'

const modules = ['editor', 'queryConn', 'queryResult', 'schemaSidebar']

export default modules.reduce((acc, m) => {
    acc[m] = queryHelper.memStateCreator(m)
    return acc
}, {})
