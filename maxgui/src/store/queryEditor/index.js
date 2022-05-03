import query, { defWorksheetState } from './query'
import queryConn from './queryConn'
import editor from './editor'
import schemaSidebar from './schemaSidebar'

export function getDefWorksheetState() {
    return defWorksheetState()
}
export default { query, queryConn, editor, schemaSidebar }
