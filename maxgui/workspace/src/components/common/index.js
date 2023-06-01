/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import '@share/components/common/MxsCharts/config.js'
import shared from '@share/components/common/shared'
import MxsPwdInput from '@wsSrc/components/common/MxsPwdInput'
import MxsSplitPane from '@wsSrc/components/common/MxsSplitPane'
import MxsSubMenu from '@wsSrc/components/common/MxsSubMenu'
import MxsSqlEditor from '@wsSrc/components/common/MxsSqlEditor'
import MxsTimeoutInput from '@wsSrc/components/common/MxsTimeoutInput/'
import MxsTreeview from '@wsSrc/components/common/MxsTreeview'
import MxsTxtFieldWithLabel from '@wsSrc/components/common/MxsTxtFieldWithLabel'
import MxsUidInput from '@wsSrc/components/common/MxsUidInput'
import MxsVirtualScrollTbl from '@wsSrc/components/common/MxsVirtualScrollTbl'

// export to register in @share dir for dev env
export const workspaceComponents = {
    'mxs-pwd-input': MxsPwdInput,
    'mxs-split-pane': MxsSplitPane,
    'mxs-sub-menu': MxsSubMenu,
    'mxs-timeout-input': MxsTimeoutInput,
    'mxs-treeview': MxsTreeview,
    'mxs-txt-field-with-label': MxsTxtFieldWithLabel,
    'mxs-uid-input': MxsUidInput,
    'mxs-virtual-scroll-tbl': MxsVirtualScrollTbl,
    'mxs-sql-editor': MxsSqlEditor,
}
export default {
    ...shared,
    ...workspaceComponents,
}
