<template>
    <mxs-dlg
        v-model="isOpened"
        :title="$mxs_t('selectObjsToVisualize')"
        saveText="visualize"
        minBodyWidth="768px"
        :allowEnterToSubmit="false"
        :hasSavingErr="hasSavingErr"
        :onSave="visualize"
    >
        <template v-slot:form-body>
            <mxs-label-field
                v-model.trim="name"
                :label="$mxs_t('name')"
                :required="true"
                class="mb-3"
            />
            <selectable-schema-table-tree
                :connId="connId"
                :preselectedSchemas="preselectedSchemas"
                :triggerDataFetch="isOpened"
                excludeNonFkSupportedTbl
                @selected-targets="selectedTargets = $event"
            />
            <div class="err-visualizing-message-ctr mt-3">
                <p class="mxs-color-helper text-small-text mb-1" data-test="erd-support-table-info">
                    {{ $mxs_t('info.erdTableSupport') }}
                </p>
                <p v-if="errMsg" class="v-messages__message error--text mt-2" data-test="err-msg">
                    {{ errMsg }}
                </p>
            </div>
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
import { mapState, mapMutations, mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import SelectableSchemaTableTree from '@wkeComps/SelectableSchemaTableTree'
import connection from '@wsSrc/api/connection'
import queryHelper from '@wsSrc/store/queryHelper'
import erdHelper from '@wsSrc/utils/erdHelper'
import { QUERY_CONN_BINDING_TYPES } from '@wsSrc/constants'

export default {
    name: 'gen-erd-dlg',
    components: { SelectableSchemaTableTree },
    data() {
        return {
            selectedTargets: [],
            errMsg: '',
            name: '',
        }
    },
    computed: {
        ...mapState({
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
            return Boolean(this.errMsg) || Boolean(!this.selectedTargets.length)
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
                } else this.errMsg = ''
            },
        },
    },
    methods: {
        ...mapMutations({ SET_GEN_ERD_DLG: 'mxsWorkspace/SET_GEN_ERD_DLG' }),
        ...mapActions({ queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData' }),
        async handleCloneConn({ conn, config }) {
            const [e, res] = await this.$helpers.to(connection.clone({ id: conn.id, config }))
            if (e) this.errMsg = this.$helpers.getErrorsArr(e).join('\n')
            return this.$typy(res, 'data.data').safeObjectOrEmpty
        },
        async handleQueryData({ conn, config }) {
            await QueryConn.dispatch('enableSqlQuoteShowCreate', { connId: conn.id, config })
            await this.queryDdlEditorSuppData({ connId: conn.id, config })
            const [e, parsedTables] = await queryHelper.queryAndParseTblDDL({
                connId: conn.id,
                targets: this.selectedTargets,
                config,
                charsetCollationMap: this.charset_collation_map,
            })
            if (e) this.errMsg = this.$helpers.getErrorsArr(e).join('\n')
            else {
                const nodeMap = parsedTables.reduce((map, parsedTable, i) => {
                    const node = erdHelper.genErdNode({
                        nodeData: parsedTable,
                        highlightColor: this.$helpers.dynamicColors(i),
                    })
                    map[node.id] = node
                    return map
                }, {})

                return {
                    erdTaskData: { nodeMap, is_laid_out: false },
                    erdTaskTmpData: {
                        graph_height_pct: 100,
                        active_entity_id: '',
                        key: this.$helpers.uuidv1(),
                        nodes_history: [],
                        active_history_idx: 0,
                    },
                }
            }
        },
        async visualize() {
            const config = this.activeRequestConfig
            const connMeta = this.conn.meta
            let conn = this.conn

            if (this.genInNewWs) {
                conn = await this.handleCloneConn({
                    conn: this.$helpers.lodash.cloneDeep(this.conn),
                    config,
                })
                await QueryConn.dispatch('setVariables', { connId: conn.id, config })
            }
            if (conn.id) {
                const data = await this.handleQueryData({ conn, config })
                if (data) {
                    const { erdTaskData, erdTaskTmpData } = data
                    if (this.genInNewWs)
                        this.visualizeInNewWs({ conn, connMeta, erdTaskData, erdTaskTmpData })
                    else this.visualizeInCurrentWs({ erdTaskData, erdTaskTmpData })
                    Worksheet.update({ where: this.activeWkeId, data: { name: this.name } })
                }
            }
        },
        visualizeInNewWs({ conn, connMeta, erdTaskData, erdTaskTmpData }) {
            Worksheet.dispatch('insertBlankWke')
            ErdTask.dispatch('initErdEntities', { erdTaskData, erdTaskTmpData })
            QueryConn.insert({
                data: {
                    id: conn.id,
                    attributes: conn.attributes,
                    binding_type: QUERY_CONN_BINDING_TYPES.ERD,
                    erd_task_id: this.activeWkeId,
                    meta: connMeta,
                },
            })
        },
        visualizeInCurrentWs({ erdTaskData, erdTaskTmpData }) {
            // Close the entity-editor-ctr before assigning new data
            ErdTaskTmp.update({
                where: this.activeWkeId,
                data: { active_entity_id: '', graph_height_pct: 100 },
            }).then(() => {
                ErdTask.update({ where: this.activeWkeId, data: erdTaskData })
                ErdTaskTmp.update({ where: this.activeWkeId, data: erdTaskTmpData })
            })
        },
    },
}
</script>

<style lang="scss" scoped>
.err-visualizing-message-ctr {
    min-height: 24px;
    .error--text {
        white-space: pre-wrap;
        line-height: 1.5rem;
    }
}
</style>
