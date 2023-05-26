<template>
    <mxs-conf-dlg
        v-model="isOpened"
        minBodyWidth="768px"
        :title="file_dlg_data.title"
        :closeImmediate="true"
        :lazyValidation="false"
        :onSave="file_dlg_data.on_save"
        cancelText="dontSave"
        saveText="save"
        @on-cancel="file_dlg_data.dont_save"
    >
        <template v-slot:form-body>
            <p v-html="file_dlg_data.confirm_msg" />
        </template>
    </mxs-conf-dlg>
</template>
<script>
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
import { mapState, mapMutations } from 'vuex'
export default {
    name: 'file-dlg-ctr',
    computed: {
        ...mapState({ file_dlg_data: state => state.editorsMem.file_dlg_data }),
        isOpened: {
            get() {
                return this.file_dlg_data.is_opened
            },
            set(value) {
                this.SET_FILE_DLG_DATA({ ...this.file_dlg_data, is_opened: value })
            },
        },
    },
    methods: { ...mapMutations({ SET_FILE_DLG_DATA: 'editorsMem/SET_FILE_DLG_DATA' }) },
}
</script>
