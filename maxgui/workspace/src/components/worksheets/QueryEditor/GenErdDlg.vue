<template>
    <mxs-conf-dlg
        v-model="isOpened"
        :title="$mxs_t('selectObjsToVisualize')"
        saveText="visualize"
        minBodyWidth="768px"
        :allowEnterToSubmit="false"
        :hasSavingErr="hasSavingErr"
        :onSave="visualize"
    >
        <template v-slot:body-prepend>
            <mxs-txt-field-with-label
                v-model.trim="name"
                :label="$mxs_t('name')"
                :required="true"
                class="mb-3"
            />
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
import ErdTask from '@wsSrc/store/orm/models/ErdTask'
import QueryConn from '@wsSrc/store/orm/models/QueryConn'
import Worksheet from '@wsSrc/store/orm/models/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import SelectableSchemaTableTree from '@wkeComps/SelectableSchemaTableTree'
import connection from '@wsSrc/api/connection'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'gen-erd-dlg',
    components: { SelectableSchemaTableTree },
    props: {
        value: { type: Boolean, required: true },
        preselectedSchemas: { type: Array, required: true },
    },
    data() {
        return {
            selectedTableNodes: [],
            errVisualizingMsg: '',
            name: '',
        }
    },
    computed: {
        ...mapState({
            parsed_ddl: state => state.editorsMem.parsed_ddl,
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
        }),
        isOpened: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        activeQueryEditorConn() {
            return QueryConn.getters('getQueryEditorConn')
        },
        activeQueryEditorConnId() {
            return this.$typy(this.activeQueryEditorConn, 'id').safeString
        },
        activeRequestConfig() {
            return Worksheet.getters('getActiveRequestConfig')
        },
        hasSavingErr() {
            return Boolean(this.errVisualizingMsg) || Boolean(!this.selectedTableNodes.length)
        },
    },
    watch: {
        isOpened: {
            handler(v) {
                if (v) {
                    const count = this.$typy(ErdTask.query().last(), 'count').safeNumber + 1
                    this.name = `ERD ${count}`
                }
            },
        },
    },
    methods: {
        ...mapActions({
            queryAndParseDDL: 'editorsMem/queryAndParseDDL',
        }),
        async cloneConn({ queryEditorConn, config }) {
            const [e, res] = await this.$helpers.to(
                connection.clone({ id: queryEditorConn.id, config })
            )
            if (e) this.errVisualizingMsg = this.$helpers.getErrorsArr(e).join('\n')
            return this.$typy(res, 'data.data').safeObjectOrEmpty
        },
        async visualize() {
            /**
             * Copy current query editor data .i.e. connection and request_config data
             * before inserting a new worksheet
             */
            const queryEditorConn = this.$helpers.lodash.cloneDeep(this.activeQueryEditorConn)
            const config = this.$helpers.lodash.cloneDeep(this.activeRequestConfig)

            const clonedConn = await this.cloneConn({ queryEditorConn, config })

            if (clonedConn.id) {
                await this.queryAndParseDDL({
                    connId: clonedConn.id,
                    tableNodes: this.selectedTableNodes,
                })
                const erdData = queryHelper.genErdData(this.parsed_ddl)
                Worksheet.dispatch('insertErdWke', this.name)
                // activeWkeId is also erd_task_id
                const activeWkeId = Worksheet.getters('getActiveWkeId')
                // Bind the cloned connection as ERD connection
                const erdConn = {
                    id: clonedConn.id,
                    attributes: clonedConn.attributes,
                    binding_type: this.QUERY_CONN_BINDING_TYPES.ERD,
                    erd_task_id: activeWkeId,
                    meta: queryEditorConn.meta,
                }
                QueryConn.insert({ data: erdConn })
                WorksheetTmp.update({ where: activeWkeId, data: { request_config: config } })
                ErdTask.update({ where: activeWkeId, data: { data: erdData } })
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
