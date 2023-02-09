<template>
    <mxs-dlg
        v-model="isOpened"
        :onSave="onSave"
        :title="dlgData.title"
        :minBodyWidth="dlgData.width"
        :saveText="migr_dlg.type"
    >
        <template v-slot:form-body>
            <template v-if="migr_dlg.type === MIGR_DLG_TYPES.CREATE">
                <label class="field__label mxs-color-helper text-small-text label-required">
                    {{ $mxs_t('name') }}
                </label>
                <v-text-field
                    v-model="name"
                    :rules="[
                        v => !!v || $mxs_t('errors.requiredInput', { inputName: $mxs_t('name') }),
                    ]"
                    required
                    :height="36"
                    autofocus
                    class="vuetify-input--override error--text__bottom etl-task-name-input"
                    dense
                    outlined
                />
            </template>
            <template v-else-if="migr_dlg.type === MIGR_DLG_TYPES.DELETE">
                <p>{{ dlgData.info }}</p>
            </template>
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
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'migr-dlg',
    data() {
        return {
            name: '',
        }
    },
    computed: {
        ...mapState({
            MIGR_DLG_TYPES: state => state.mxsWorkspace.config.MIGR_DLG_TYPES,
            migr_dlg: state => state.mxsWorkspace.migr_dlg,
        }),
        isOpened: {
            get() {
                return this.migr_dlg.is_opened
            },
            set(v) {
                this.SET_MIGR_DLG({ ...this.migr_dlg, is_opened: v })
            },
        },
        dlgData() {
            const { CREATE, DELETE } = this.MIGR_DLG_TYPES
            let title = '',
                info = '',
                width = '512px'
            switch (this.migr_dlg.type) {
                case CREATE:
                    title = this.$mxs_t('newMigration')
                    width = '512px'
                    break
                case DELETE:
                    title = this.$mxs_t('confirmations.deleteEtl')
                    info = this.$mxs_t('info.deleteEtlTask')
                    width = '624px'
            }
            return { title, info, width }
        },
    },
    watch: {
        isOpened(v) {
            if (v) this.name = this.$mxs_t('newMigration')
        },
    },
    methods: {
        ...mapMutations({ SET_MIGR_DLG: 'mxsWorkspace/SET_MIGR_DLG' }),
        async onSave() {
            const { CREATE, DELETE } = this.MIGR_DLG_TYPES
            const { type, etl_task_id } = this.migr_dlg
            switch (type) {
                case CREATE:
                    EtlTask.dispatch('createEtlTask', this.name)
                    break
                case DELETE: {
                    await QueryConn.dispatch('disconnectConnsFromTask', etl_task_id)
                    const wke = Worksheet.query()
                        .where('etl_task_id', etl_task_id)
                        .first()
                    if (wke) await Worksheet.dispatch('handleDeleteWke', wke.id)
                    EtlTask.dispatch('cascadeDelete', etl_task_id)
                    break
                }
            }
        },
    },
}
</script>
