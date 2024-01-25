<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="$mxs_t('confirmations.deleteEtl')"
        minBodyWidth="624px"
        :saveText="migr_dlg.type"
    >
        <template v-slot:form-body>
            <p>{{ $mxs_t('info.deleteEtlTask') }}</p>
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { mapMutations, mapState } from 'vuex'
import { MIGR_DLG_TYPES } from '@wsSrc/constants'

export default {
    name: 'migr-delete-dlg',
    computed: {
        ...mapState({ migr_dlg: state => state.mxsWorkspace.migr_dlg }),
        isOpened: {
            get() {
                const { type, is_opened } = this.migr_dlg
                return type === MIGR_DLG_TYPES.DELETE ? is_opened : false
            },
            set(v) {
                this.SET_MIGR_DLG({ ...this.migr_dlg, is_opened: v })
            },
        },
        etlTaskWke() {
            return Worksheet.query()
                .where('etl_task_id', this.migr_dlg.etl_task_id)
                .first()
        },
    },
    methods: {
        ...mapMutations({ SET_MIGR_DLG: 'mxsWorkspace/SET_MIGR_DLG' }),
        async onSave() {
            const { etl_task_id } = this.migr_dlg
            await QueryConn.dispatch('disconnectConnsFromTask', etl_task_id)
            if (this.etlTaskWke) await Worksheet.dispatch('handleDeleteWke', this.etlTaskWke.id)
            EtlTask.dispatch('cascadeDelete', etl_task_id)
        },
    },
}
</script>
