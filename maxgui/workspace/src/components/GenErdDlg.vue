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
                :connId="connId"
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
import { mapState, mapMutations, mapActions } from 'vuex'
import ErdTask from '@wsSrc/store/orm/models/ErdTask'
import ErdTaskTmp from '@wsSrc/store/orm/models/ErdTaskTmp'
import QueryConn from '@wsSrc/store/orm/models/QueryConn'
import Worksheet from '@wsSrc/store/orm/models/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import SelectableSchemaTableTree from '@wkeComps/SelectableSchemaTableTree'
import connection from '@wsSrc/api/connection'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    name: 'gen-erd-dlg',
    components: { SelectableSchemaTableTree },
    data() {
        return {
            selectedTableNodes: [],
            errVisualizingMsg: '',
            name: '',
        }
    },
    computed: {
        ...mapState({
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
            gen_erd_dlg: state => state.mxsWorkspace.gen_erd_dlg,
            charset_collation_map: state => state.editorsMem.charset_collation_map,
        }),
        isOpened: {
            get() {
                return this.gen_erd_dlg.is_opened
            },
            set(v) {
                this.SET_GEN_ERD_DLG({ ...this.gen_erd_dlg, is_opened: v })
            },
        },
        preselectedSchemas() {
            return this.gen_erd_dlg.preselected_schemas
        },
        conn() {
            return this.$typy(this.gen_erd_dlg, 'connection').safeObject
        },
        connId() {
            return this.$typy(this.conn, 'id').safeString
        },
        genInNewWs() {
            return this.$typy(this.gen_erd_dlg, 'gen_in_new_ws').safeBoolean
        },
        activeRequestConfig() {
            return Worksheet.getters('activeRequestConfig')
        },
        hasSavingErr() {
            return Boolean(this.errVisualizingMsg) || Boolean(!this.selectedTableNodes.length)
        },
        activeWkeId() {
            return Worksheet.getters('activeId') // activeWkeId is also erd_task_id
        },
    },
    watch: {
        isOpened: {
            handler(v) {
                if (v) {
                    const { id, count = 0 } = ErdTask.query().last() || {}
                    this.name = `ERD ${this.activeWkeId === id ? count : count + 1}`
                }
            },
        },
    },
    methods: {
        ...mapMutations({ SET_GEN_ERD_DLG: 'mxsWorkspace/SET_GEN_ERD_DLG' }),
        ...mapActions({ queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData' }),
        async cloneConn({ conn, config }) {
            const [e, res] = await this.$helpers.to(connection.clone({ id: conn.id, config }))
            if (e) this.errVisualizingMsg = this.$helpers.getErrorsArr(e).join('\n')
            return this.$typy(res, 'data.data').safeObjectOrEmpty
        },
        async visualize() {
            const config = this.$helpers.lodash.cloneDeep(this.activeRequestConfig)
            let conn = this.$helpers.lodash.cloneDeep(this.conn),
                connMeta = conn.meta,
                activeWkeId = this.activeWkeId
            /**
             * Clone the current connection and request_config data
             */
            if (this.genInNewWs) conn = await this.cloneConn({ conn, config })
            if (conn.id) {
                const [, parsedDdl] = await queryHelper.queryAndParseDDL({
                    connId: conn.id,
                    tableNodes: this.selectedTableNodes,
                    config,
                })
                await this.queryDdlEditorSuppData({ connId: conn.id, config })
                const erdTaskData = {
                    data: queryHelper.genErdData({
                        data: parsedDdl,
                        charsetCollationMap: this.charset_collation_map,
                    }),
                    is_laid_out: false,
                }
                const erdTaskTmpData = {
                    graph_height_pct: 100,
                    active_entity_id: '',
                    data: erdTaskData.data,
                    key: this.$helpers.uuidv1(),
                    graph_data_history: [],
                    active_history_idx: 0,
                }
                // Bind connection to a new worksheet
                if (this.genInNewWs) {
                    Worksheet.dispatch('insertBlankWke')
                    ErdTask.dispatch('initErdEntities', { erdTaskData, erdTaskTmpData })
                    activeWkeId = Worksheet.getters('activeId')
                    QueryConn.insert({
                        data: {
                            id: conn.id,
                            attributes: conn.attributes,
                            binding_type: this.QUERY_CONN_BINDING_TYPES.ERD,
                            erd_task_id: activeWkeId,
                            meta: connMeta,
                        },
                    })
                } else {
                    ErdTask.update({ where: activeWkeId, data: erdTaskData })
                    ErdTaskTmp.update({ where: activeWkeId, data: erdTaskTmpData })
                }

                WorksheetTmp.update({ where: activeWkeId, data: { request_config: config } })
                Worksheet.update({ where: activeWkeId, data: { name: this.name } })
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
