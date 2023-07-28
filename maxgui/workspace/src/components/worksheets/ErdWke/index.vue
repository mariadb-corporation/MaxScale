<template>
    <div class="fill-height">
        <mxs-split-pane
            v-model="graphHeightPct"
            :boundary="dim.height"
            split="horiz"
            :minPercent="minErdPct"
            :maxPercent="maxErdPct"
            :deactivatedMaxPctZone="maxErdPct - (100 - maxErdPct) * 2"
            :disable="graphHeightPct === 100"
        >
            <template slot="pane-left">
                <diagram-ctr
                    ref="diagramCtr"
                    :dim="erdDim"
                    :hasValidChanges="hasValidChanges"
                    :connId="connId"
                    :newTableMap="newTableMap"
                    :updatedTableMap="updatedTableMap"
                    :isFormValid="isFormValid"
                    @on-apply-script="applyScript"
                    @on-export-script="exportScript"
                    @on-export-as-jpeg="exportAsJpeg"
                />
            </template>
            <template slot="pane-right">
                <entity-editor-ctr
                    v-show="activeEntityId"
                    :dim="editorDim"
                    @is-form-valid="isFormValid = $event"
                />
            </template>
        </mxs-split-pane>
    </div>
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
import { mapMutations, mapState, mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsSrc/store/orm/models/Worksheet'
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'
import TableScriptBuilder from '@wsSrc/utils/TableScriptBuilder.js'

export default {
    name: 'erd-wke',
    components: { DiagramCtr, EntityEditorCtr },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            scriptGeneratedTime: null,
            isFormValid: true,
        }
    },
    computed: {
        ...mapState({
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
            CREATE_TBL_TOKENS: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
        }),
        dim() {
            const { width, height } = this.ctrDim
            return { width: width, height: height }
        },
        erdDim() {
            return { width: this.ctrDim.width, height: this.erGraphHeight }
        },
        erGraphHeight() {
            return this.$helpers.pctToPx({
                pct: this.graphHeightPct,
                containerPx: this.dim.height,
            })
        },
        editorDim() {
            return { width: this.ctrDim.width, height: this.dim.height - this.erGraphHeight }
        },
        minErdPct() {
            return this.$helpers.pxToPct({
                px: this.activeEntityId ? 42 : 0,
                containerPx: this.dim.height,
            })
        },
        maxErdPct() {
            return 100 - this.minErdPct
        },
        activeEntityId() {
            return ErdTask.getters('activeEntityId')
        },
        activeTaskId() {
            return ErdTask.getters('activeRecordId')
        },
        taskName() {
            return this.$typy(Worksheet.getters('activeRecord'), 'name').safeString
        },
        graphHeightPct: {
            get() {
                return ErdTask.getters('graphHeightPct')
            },
            set(v) {
                ErdTaskTmp.update({
                    where: this.activeTaskId,
                    data: { graph_height_pct: v },
                })
            },
        },
        activeErdConn() {
            return QueryConn.getters('activeErdConn')
        },
        connId() {
            return this.$typy(this.activeErdConn, 'id').safeString
        },
        stagingNodes() {
            return ErdTask.getters('stagingNodes')
        },
        tableDiffs() {
            return this.$helpers.arrOfObjsDiff({
                base: ErdTask.getters('initialTables'),
                newArr: ErdTask.getters('stagingTables'),
                idField: 'id',
            })
        },
        newTableMap() {
            return this.$helpers.lodash.keyBy(this.tableDiffs.get('added'), 'id')
        },
        updatedTableMap() {
            return this.$helpers.lodash.keyBy(
                this.tableDiffs.get('updated').map(n => n.newObj),
                'id'
            )
        },
        hasChanged() {
            return (
                !this.$typy(this.updatedTableMap).isEmptyObject ||
                !this.$typy(this.newTableMap).isEmptyObject ||
                Boolean(this.tableDiffs.get('removed').length)
            )
        },
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
        blockCmt() {
            return '# ============================================================================='
        },
        halfBlockCmt() {
            return this.blockCmt.slice(0, Math.round(this.blockCmt.length / 2))
        },
        newSchemas() {
            const { xorWith, isEqual } = this.$helpers.lodash
            return xorWith(
                ErdTask.getters('initialSchemas'),
                ErdTask.getters('stagingSchemas'),
                isEqual
            )
        },
        scriptName() {
            return 'Generated by MaxScale GUI'
        },
        scriptTrademark() {
            return [
                this.blockCmt,
                `# ${this.scriptName}`,
                `# ${this.scriptGeneratedTime}`,
                this.blockCmt,
            ].join('\n')
        },
    },
    methods: {
        ...mapActions({ exeDdlScript: 'mxsWorkspace/exeDdlScript' }),
        ...mapMutations({ SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG' }),
        createSectionCmt(name) {
            return `# ${name}\n${this.halfBlockCmt}`
        },
        genScript() {
            this.scriptGeneratedTime = this.$helpers.dateFormat({ value: new Date() })
            const tablesColNameMap = ErdTask.getters('tablesColNameMap')
            const refTargetMap = ErdTask.getters('refTargetMap')
            const { formatSQL, quotingIdentifier: quoting } = this.$helpers
            let parts = [],
                newTablesFks = [],
                alterTableParts = []

            // updated tables
            this.tableDiffs.get('updated').forEach(({ newObj, oriObj }) => {
                const builder = new TableScriptBuilder({
                    initialData: oriObj,
                    stagingData: newObj,
                    refTargetMap,
                    tablesColNameMap,
                    options: { skipFkCreation: true },
                })
                const script = builder.build()
                if (script) alterTableParts.push(script)
                const fks = builder.buildNewFkSQL()
                if (fks) newTablesFks.push(fks)
            })
            if (alterTableParts.length) {
                alterTableParts.unshift(this.createSectionCmt('Alter tables'))
                parts = [...parts, ...alterTableParts]
            }

            // Drop tables
            this.tableDiffs.get('removed').forEach((tbl, i) => {
                if (i === 0) parts.push(this.createSectionCmt('Drop tables'))
                const schema = quoting(tbl.options.schema)
                const name = quoting(tbl.options.name)
                parts.push(`DROP TABLE ${schema}.${name};`)
            })

            // new schemas
            this.newSchemas.forEach((s, i) => {
                if (i === 0) parts.push(this.createSectionCmt('Create schemas'))
                const schema = quoting(s)
                parts.push(`CREATE SCHEMA IF NOT EXISTS ${schema};`)
            })
            // new tables
            this.tableDiffs.get('added').forEach((tbl, i) => {
                if (i === 0) parts.push(this.createSectionCmt('Create tables'))
                const builder = new TableScriptBuilder({
                    initialData: {},
                    stagingData: tbl,
                    refTargetMap,
                    tablesColNameMap,
                    options: {
                        isCreating: true,
                        skipSchemaCreation: true,
                        skipFkCreation: true,
                    },
                })
                parts.push(builder.build())
                const fks = builder.buildNewFkSQL()
                if (fks) newTablesFks.push(fks)
            })

            if (newTablesFks.length) {
                parts.push(this.createSectionCmt('Add new tables constraints'))
                parts.push(newTablesFks.join(''))
            }

            let sql = formatSQL(parts.join('\n'))
            sql = `${this.scriptTrademark}\n\n${sql}`
            return sql
        },
        applyScript() {
            this.SET_EXEC_SQL_DLG({
                ...this.exec_sql_dlg,
                is_opened: true,
                editor_height: 450,
                sql: this.genScript(),
                on_exec: this.onExecuteScript,
                on_after_cancel: () =>
                    this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null }),
            })
        },
        async onExecuteScript() {
            await this.exeDdlScript({
                connId: this.connId,
                actionName: `Apply script ${this.scriptName} at ${this.scriptGeneratedTime}`,
                successCb: () => {
                    ErdTask.update({
                        where: this.activeTaskId,
                        data: { tables: this.stagingNodes.map(n => n.data) },
                    })
                    ErdTask.dispatch('setNodesHistory', [this.stagingNodes])
                },
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
    },
}
</script>
