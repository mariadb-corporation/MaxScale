<template>
    <div>
        <query-editor ref="queryEditor" />
        <overlay />
        <snackbars :msgObj="snackbar_message" />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import '@queryEditorSrc/styles/main.scss'
import Overlay from '@share/components/overlay'
import Snackbars from '@share/components/Snackbars'
import QueryEditor from '@queryEditorSrc/QueryEditor.vue'
import { mapState } from 'vuex'

export default /*#__PURE__*/ {
    name: 'maxscale-query-editor',
    components: { QueryEditor, Overlay, Snackbars },
    computed: {
        ...mapState({ snackbar_message: state => state.appNotifier.snackbar_message }),
    },
}
</script>
