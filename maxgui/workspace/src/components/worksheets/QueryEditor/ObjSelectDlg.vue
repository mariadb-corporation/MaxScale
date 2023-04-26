<template>
    <mxs-conf-dlg
        v-model="isOpened"
        :title="$mxs_t('selectObjsToVisualize')"
        saveText="visualize"
        minBodyWidth="768px"
        allowEnterToSubmit
        :hasSavingErr="hasSavingErr"
        :onSave="visualize"
    >
        <template v-slot:body-prepend>
            <selectable-schema-table-tree
                v-model="selectedObjs"
                :connId="activeQueryEditorConnId"
                :preselectedSchemas="preselectedSchemas"
                :shouldRefresh="isOpened"
            />
        </template>
    </mxs-conf-dlg>
</template>
<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryConn from '@wsSrc/store/orm/models/QueryConn'
import SelectableSchemaTableTree from '@wkeComps/SelectableSchemaTableTree'

export default {
    name: 'obj-select-dlg',
    components: { SelectableSchemaTableTree },
    props: {
        value: { type: Boolean, required: true },
        preselectedSchemas: { type: Array, required: true },
    },
    data() {
        return {
            selectedObjs: [],
            visualizingErr: '',
        }
    },
    computed: {
        isOpened: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        activeQueryEditorConnId() {
            return this.$typy(QueryConn.getters('getQueryEditorConn'), 'id').safeString
        },
        hasSavingErr() {
            return Boolean(this.visualizingErr)
        },
    },
    methods: {
        /**
         * TODO: Fetch DDL for chosen tables.
         * Parse DDL.
         * Generate ERD data.
         * Clone current active query editor connection and use
         * that for opening a new ERD worksheet.
         */
        visualize() {},
    },
}
</script>
