import { uniqueId } from 'utils/helpers'
import queryHelper from './queryHelper'

/**
 * @returns Return a new worksheet state
 */
export function defWorksheetState() {
    return {
        id: uniqueId(`WORKSHEET_${new Date().getUTCMilliseconds()}_`),
        name: 'WORKSHEET',
        ...queryHelper.syncStateCreator('editor'),
        ...queryHelper.syncStateCreator('queryResult'),
        ...queryHelper.syncStateCreator('schemaSidebar'),
    }
}
/**
 * @returns Return a new session state
 */
export function defSessionState(wke_id) {
    return {
        id: uniqueId(`SESSION_${new Date().getUTCMilliseconds()}_`),
        name: 'Query Tab 1',
        wke_id_fk: wke_id,
        count: 1,
        ...queryHelper.syncStateCreator('queryConn'),
    }
}

const get_def_worksheets_arr = [defWorksheetState()]
const get_def_query_sessions = [defSessionState(get_def_worksheets_arr[0].id)]

export default {
    get_def_worksheets_arr,
    get_def_query_sessions,
}
