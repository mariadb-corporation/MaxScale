<template>
    <mxs-dlg
        v-model="isOpened"
        minBodyWidth="768px"
        :title="confirm_dlg.title"
        :closeImmediate="true"
        :lazyValidation="false"
        :onSave="confirm_dlg.on_save"
        :cancelText="confirm_dlg.cancel_text"
        :saveText="confirm_dlg.save_text"
        @after-cancel="confirm_dlg.after_cancel"
    >
        <template v-slot:form-body>
            <p v-html="confirm_dlg.confirm_msg" />
        </template>
    </mxs-dlg>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'
export default {
    name: 'confirm-dlg',
    computed: {
        ...mapState({ confirm_dlg: state => state.mxsWorkspace.confirm_dlg }),
        isOpened: {
            get() {
                return this.confirm_dlg.is_opened
            },
            set(value) {
                this.SET_CONFIRM_DLG({ ...this.confirm_dlg, is_opened: value })
            },
        },
    },
    methods: { ...mapMutations({ SET_CONFIRM_DLG: 'mxsWorkspace/SET_CONFIRM_DLG' }) },
}
</script>
