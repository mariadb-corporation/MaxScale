//TODO: Remove this file once ORM replacement is done
import queryHelper from '@queryEditorSrc/store/queryHelper'

const modules = ['queryResult']

export default modules.reduce((acc, m) => {
    acc[m] = queryHelper.memStateCreator(m)
    return acc
}, {})
