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
                :connId="activeQueryEditorConnId"
                :preselectedSchemas="preselectedSchemas"
                :triggerDataFetch="isOpened"
                @selected-tables="selectedTableNodes = $event"
            />
            <div class="err-visualizing-message-ctr mt-3">
                <p v-if="errVisualizingMsg" class="v-messages__message error--text">
                    {{ errVisualizingMsg }}
                </p>
            </div>
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
import { mapState, mapActions } from 'vuex'
import QueryConn from '@wsSrc/store/orm/models/QueryConn'
import Worksheet from '@wsSrc/store/orm/models/Worksheet'
import SelectableSchemaTableTree from '@wkeComps/SelectableSchemaTableTree'
import connection from '@wsSrc/api/connection'

export default {
    name: 'obj-select-dlg',
    components: { SelectableSchemaTableTree },
    props: {
        value: { type: Boolean, required: true },
        preselectedSchemas: { type: Array, required: true },
    },
    data() {
        return {
            selectedTableNodes: [],
            errVisualizingMsg: '',
        }
    },
    computed: {
        ...mapState({
            parsed_ddl: state => state.editorsMem.parsed_ddl,
        }),
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
        activeRequestConfig() {
            return Worksheet.getters('getActiveRequestConfig')
        },
        hasSavingErr() {
            return Boolean(this.errVisualizingMsg) || Boolean(!this.selectedTableNodes.length)
        },
    },
    methods: {
        ...mapActions({
            queryAndParseDDL: 'editorsMem/queryAndParseDDL',
        }),
        async cloneConn() {
            const [e, res] = await this.$helpers.to(
                connection.clone({
                    id: this.activeQueryEditorConnId,
                    config: this.activeRequestConfig,
                })
            )
            if (e) this.errVisualizingMsg = this.$helpers.getErrorsArr(e).join('\n')
            return this.$typy(res, 'data.data').safeObjectOrEmpty
        },
        /**
         * TODO:
         * Generate ERD data from parsed_ddl
         * Open new ERD worksheet and bind cloned connection
         */
        async visualize() {
            const conn = await this.cloneConn()
            if (conn.id) {
                await this.queryAndParseDDL({
                    connId: conn.id,
                    tableNodes: this.selectedTableNodes,
                })
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.err-visualizing-message-ctr {
    min-height: 24px;
}
</style>
