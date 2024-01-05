<template>
    <mxs-split-pane
        v-model="graphHeightPct"
        :boundary="ctrDim.height"
        split="horiz"
        :minPercent="minErdPct"
        :maxPercent="maxErdPct"
        :deactivatedMaxPctZone="maxErdPct - (100 - maxErdPct) * 2"
        :disable="graphHeightPct === 100"
    >
        <template slot="pane-left">
            <diagram-ctr
                ref="diagramCtr"
                :isFormValid="isFormValid"
                :dim="erdDim"
                :graphHeightPct="graphHeightPct"
                :erdTask="erdTask"
                :conn="conn"
                :nodeMap="nodeMap"
                :nodes="nodes"
                :tables="tables"
                :schemas="schemas"
                :activeEntityId="activeEntityId"
                :erdTaskTmp="erdTaskTmp"
                :refTargetMap="refTargetMap"
                :tablesColNameMap="tablesColNameMap"
                @on-apply-script="applyScript"
                @on-export-script="exportScript"
                @on-export-as-jpeg="exportAsJpeg"
                @on-copy-script-to-clipboard="copyScriptToClipboard"
            />
        </template>
        <template slot="pane-right">
            <entity-editor-ctr
                v-show="activeEntityId"
                :dim="editorDim"
                :taskId="taskId"
                :connId="connId"
                :nodeMap="nodeMap"
                :tables="tables"
                :schemas="schemas"
                :activeEntityId="activeEntityId"
                :erdTaskTmp="erdTaskTmp"
                @is-form-valid="isFormValid = $event"
            />
        </template>
    </mxs-split-pane>
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
import { mapMutations, mapState, mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'
import TableScriptBuilder from '@wsSrc/utils/TableScriptBuilder.js'
import SqlCommenter from '@wsSrc/utils/SqlCommenter.js'
import erdHelper from '@wsSrc/utils/erdHelper'

export default {
    name: 'erd-wke',
    components: { DiagramCtr, EntityEditorCtr },
    props: {
        ctrDim: { type: Object, required: true },
        wke: { type: Object, required: true },
    },
    data() {
        return {
            scriptName: '',
            scriptGeneratedTime: null,
            sqlCommenter: null,
            isFormValid: true,
        }
    },
    computed: {
        ...mapState({
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
        }),
        taskId() {
            return this.wke.erd_task_id
        },
        erdTask() {
            return ErdTask.find(this.taskId) || {}
        },
        erdTaskTmp() {
            return ErdTaskTmp.find(this.taskId) || {}
        },
        nodeMap() {
            return this.$typy(this.erdTask, 'nodeMap').safeObjectOrEmpty
        },
        nodes() {
            return Object.values(this.nodeMap)
        },
        tables() {
            return this.nodes.map(n => n.data)
        },
        schemas() {
            return [...new Set(this.nodes.map(n => n.data.options.schema))]
        },
        refTargetMap() {
            return this.$helpers.lodash.keyBy(erdHelper.genRefTargets(this.tables), 'id')
        },
        tablesColNameMap() {
            return erdHelper.createTablesColNameMap(this.tables)
        },
        erdDim() {
            return { width: this.ctrDim.width, height: this.erGraphHeight }
        },
        erGraphHeight() {
            return this.$helpers.pctToPx({
                pct: this.graphHeightPct,
                containerPx: this.ctrDim.height,
            })
        },
        editorDim() {
            return { width: this.ctrDim.width, height: this.ctrDim.height - this.erGraphHeight }
        },
        minErdPct() {
            return this.$helpers.pxToPct({
                px: this.activeEntityId ? 40 : 0,
                containerPx: this.ctrDim.height,
            })
        },
        maxErdPct() {
            return 100 - this.minErdPct
        },
        activeEntityId() {
            return this.$typy(this.erdTaskTmp, 'active_entity_id').safeString
        },
        taskName() {
            return this.wke.name
        },
        graphHeightPct: {
            get() {
                return this.$typy(this.erdTaskTmp, 'graph_height_pct').safeNumber
            },
            set(v) {
                ErdTaskTmp.update({ where: this.taskId, data: { graph_height_pct: v } })
            },
        },
        conn() {
            return (
                QueryConn.query()
                    .where('erd_task_id', this.taskId)
                    .first() || {}
            )
        },
        connId() {
            return this.$typy(this.conn, 'id').safeString
        },
    },
    methods: {
        ...mapActions({ exeDdlScript: 'mxsWorkspace/exeDdlScript' }),
        ...mapMutations({ SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG' }),
        genScript() {
            this.sqlCommenter = new SqlCommenter()
            const { formatSQL, quotingIdentifier: quoting } = this.$helpers
            let parts = [],
                tablesFks = []
            // new schemas
            this.schemas.forEach((s, i) => {
                if (i === 0) parts.push(this.sqlCommenter.genSection('Create schemas'))
                const schema = quoting(s)
                parts.push(`CREATE SCHEMA IF NOT EXISTS ${schema};`)
            })
            // new tables
            this.tables.forEach((tbl, i) => {
                if (i === 0) parts.push(this.sqlCommenter.genSection('Create tables'))
                const builder = new TableScriptBuilder({
                    initialData: {},
                    stagingData: tbl,
                    refTargetMap: this.refTargetMap,
                    tablesColNameMap: this.tablesColNameMap,
                    options: {
                        isCreating: true,
                        skipSchemaCreation: true,
                        skipFkCreation: true,
                    },
                })
                parts.push(builder.build())
                const fks = builder.buildNewFkSQL()
                if (fks) tablesFks.push(fks)
            })

            if (tablesFks.length) {
                parts.push(this.sqlCommenter.genSection('Add new tables constraints'))
                parts.push(tablesFks.join(''))
            }

            const { name, time, content } = this.sqlCommenter.genHeader()
            this.scriptName = name
            this.scriptGeneratedTime = time
            let sql = formatSQL(parts.join('\n'))
            sql = `${content}\n\n${sql}`
            return sql
        },
        applyScript() {
            this.SET_EXEC_SQL_DLG({
                ...this.exec_sql_dlg,
                is_opened: true,
                editor_height: 450,
                sql: this.genScript(),
                on_exec: this.onExecuteScript,
                after_cancel: () => this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null }),
            })
        },
        async onExecuteScript() {
            await this.exeDdlScript({
                connId: this.connId,
                actionName: `Apply script ${this.scriptName} at ${this.scriptGeneratedTime}`,
            })
        },
        exportScript() {
            const blob = new Blob([this.genScript()], { type: 'text/sql' })
            const url = URL.createObjectURL(blob)
            const a = document.createElement('a')
            a.href = url
            const time = this.$helpers.dateFormat({
                value: this.scriptGeneratedTime,
                formatType: 'EEE_dd_MMM_yyyy',
            })
            a.download = `${this.taskName}_${time}.sql`
            a.click()
            URL.revokeObjectURL(url)
        },
        async exportAsJpeg() {
            const canvas = await this.$refs.diagramCtr.$refs.diagram.getCanvas()
            this.$helpers.exportToJpeg({ canvas, fileName: this.taskName })
        },
        copyScriptToClipboard() {
            this.$helpers.copyTextToClipboard(this.genScript())
        },
    },
}
</script>
