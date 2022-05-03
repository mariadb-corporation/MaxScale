import wke, { defWorksheetState } from './wke'
import queryConn from './queryConn'
import editor from './editor'
import schemaSidebar from './schemaSidebar'
import queryResult from './queryResult'

export function getDefWorksheetState() {
    return defWorksheetState()
}
export default { wke, queryConn, editor, schemaSidebar, queryResult }
