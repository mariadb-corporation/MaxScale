<template>
    <div class="fill-height mxs-color-helper border-top-table-border er-editor-ctr">
        <mxs-ddl-editor
            v-if="stagingData"
            v-model="stagingData"
            :dim="dim"
            :initialData="initialData"
            :mode="editorMode"
            :onExecute="onExecute"
        >
            <template v-slot:apply-btn-prepend="{ isFormValid }">
                <mxs-tooltip-btn
                    btnClass="toolbar-square-btn"
                    text
                    color="primary"
                    :disabled="!(isFormValid && hasChanges)"
                    @click="saveStagingData"
                >
                    <template v-slot:btn-content>
                        <v-icon size="20">mdi-content-save-outline</v-icon>
                    </template>
                    {{ $mxs_t('saveChanges') }}
                </mxs-tooltip-btn>
            </template>
            <template v-slot:toolbar-append="{ isFormValid }">
                <v-spacer />
                <!-- TODO: If isFormValid && hasChanges, it should show a dialog to prevent the
                user from accidentally discard the changes without saving or apply the changes-->
                <mxs-tooltip-btn btnClass="toolbar-square-btn" text color="error" @click="close">
                    <template v-slot:btn-content>
                        <v-icon size="12" color="error"> $vuetify.icons.mxs_close</v-icon>
                    </template>
                    {{
                        isFormValid && hasChanges ? $mxs_t('info.closeDdlEditor') : $mxs_t('close')
                    }}
                </mxs-tooltip-btn>
            </template>
        </mxs-ddl-editor>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import { EventBus } from '@wkeComps/EventBus'

export default {
    name: 'entity-editor-ctr',
    props: {
        dim: { type: Object, required: true },
    },
    data() {
        return {
            stagingData: null,
        }
    },
    computed: {
        ...mapState({
            DDL_EDITOR_MODES: state => state.mxsWorkspace.config.DDL_EDITOR_MODES,
        }),
        editorMode() {
            if (ErdTask.getters('isNewEntity')) return this.DDL_EDITOR_MODES.CREATE
            return this.DDL_EDITOR_MODES.ALTER
        },
        activeErdConnId() {
            return this.$typy(QueryConn.getters('activeErdConn'), 'id').safeString
        },
        activeTaskId() {
            return ErdTask.getters('activeRecordId')
        },
        initialEntities() {
            return ErdTask.getters('initialNodes')
        },
        stagingEntities() {
            return ErdTask.getters('stagingNodes')
        },
        activeEntityId() {
            return ErdTask.getters('activeEntityId')
        },
        initialActiveEntity() {
            return this.initialEntities.find(item => item.id === this.activeEntityId)
        },
        stagingActiveEntity() {
            return this.stagingEntities.find(item => item.id === this.activeEntityId)
        },
        // persisted data
        initialData() {
            return this.$typy(this.initialActiveEntity, 'data').safeObjectOrEmpty
        },
        // initial staging data
        stagingInitialData() {
            return this.$typy(this.stagingActiveEntity, 'data').safeObjectOrEmpty
        },
        hasChanges() {
            return !this.$helpers.lodash.isEqual(this.stagingInitialData, this.stagingData)
        },
        eventBus() {
            return EventBus
        },
    },
    async created() {
        await this.queryDdlEditorSuppData({
            connId: this.activeErdConnId,
            config: Worksheet.getters('activeRequestConfig'),
        })
    },
    activated() {
        this.watch_activeEntityId()
    },
    deactivated() {
        this.$typy(this.unwatch_activeEntityId).safeFunction()
    },
    methods: {
        ...mapActions({
            queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData',
            confirmAlter: 'mxsWorkspace/confirmAlter',
        }),
        //Watcher to work with multiple worksheets which are kept alive
        watch_activeEntityId() {
            this.unwatch_activeEntityId = this.$watch(
                'activeEntityId',
                (v, oV) => {
                    if ((v && this.$typy(this.stagingData).isNull) || (v && oV)) {
                        this.stagingData = this.$helpers.lodash.cloneDeep(this.stagingInitialData)
                    }
                },
                { immediate: true }
            )
        },
        close() {
            ErdTaskTmp.update({
                where: this.activeTaskId,
                data: { graph_height_pct: 100, active_entity_id: '' },
            })
        },
        /**
         * @param {object} model - Either ErdTask or ErdTaskTmp model
         */
        save({ model }) {
            const {
                immutableUpdate,
                lodash: { cloneDeep },
            } = this.$helpers
            const activeEntityId = this.activeEntityId
            const data = cloneDeep(this.stagingData)
            model.update({
                where: this.activeTaskId,
                data(task) {
                    const idx = task.data.nodes.findIndex(n => n.id === activeEntityId)
                    task.data.nodes = immutableUpdate(task.data.nodes, {
                        [idx]: { data: { $set: data } },
                    })
                },
            })
            this.eventBus.$emit('entity-editor-ctr-update-node-data', { id: activeEntityId, data })
        },
        async onExecute() {
            await this.confirmAlter({
                connId: this.activeErdConnId,
                schema: this.initialData.options.schema,
                name: this.initialData.options.name,
                successCb: () => {
                    this.save({ model: ErdTask })
                },
            })
        },
        saveStagingData() {
            this.save({ model: ErdTaskTmp })
        },
    },
}
</script>
