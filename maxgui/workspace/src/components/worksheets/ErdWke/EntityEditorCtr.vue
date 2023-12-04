<template>
    <mxs-ddl-editor
        v-if="stagingData"
        v-model="stagingData"
        class="fill-height mxs-color-helper border-top-table-border er-editor-ctr"
        :activeSpec.sync="activeSpec"
        :dim="dim"
        :initialData="{}"
        isCreating
        :schemas="schemas"
        :lookupTables="lookupTables"
        :connData="{ id: connId, config: activeRequestConfig }"
        :showApplyBtn="false"
        v-on="$listeners"
    >
        <template v-slot:toolbar-append="{ formRef }">
            <v-spacer />
            <mxs-tooltip-btn
                btnClass="toolbar-square-btn"
                text
                color="error"
                @click="close(formRef)"
            >
                <template v-slot:btn-content>
                    <v-icon size="12" color="error"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('close') }}
            </mxs-tooltip-btn>
        </template>
    </mxs-ddl-editor>
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
import { mapActions, mapMutations } from 'vuex'
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import { EventBus } from '@wkeComps/EventBus'

export default {
    name: 'entity-editor-ctr',
    props: {
        dim: { type: Object, required: true },
        taskId: { type: String, required: true },
        connId: { type: String, required: true },
        nodeMap: { type: Object, required: true },
        tables: { type: Array, required: true },
        schemas: { type: Array, required: true },
        activeEntityId: { type: String, required: true },
        erdTaskTmp: { type: Object, required: true },
    },
    data() {
        return {
            stagingData: null,
        }
    },
    computed: {
        lookupTables() {
            return this.$helpers.lodash.keyBy(this.tables, 'id')
        },
        stagingActiveNode() {
            return this.nodeMap[this.activeEntityId]
        },
        stagingInitialData() {
            return this.$typy(this.stagingActiveNode, 'data').safeObjectOrEmpty
        },
        eventBus() {
            return EventBus
        },
        activeRequestConfig() {
            return Worksheet.getters('activeRequestConfig')
        },
        activeSpec: {
            get() {
                return this.$typy(this.erdTaskTmp, 'active_spec').safeString
            },
            set(v) {
                ErdTaskTmp.update({ where: this.taskId, data: { active_spec: v } })
            },
        },
    },
    watch: {
        stagingInitialData: {
            deep: true,
            immediate: true,
            handler(v) {
                if (
                    !this.$typy(v).isEmptyObject &&
                    !this.$helpers.lodash.isEqual(v, this.stagingData)
                ) {
                    this.$typy(this.unwatch_stagingData).safeFunction()
                    this.stagingData = this.$helpers.lodash.cloneDeep(v)
                    this.watch_stagingData()
                }
            },
        },
        connId: {
            immediate: true,
            async handler(v) {
                if (v)
                    await this.queryDdlEditorSuppData({
                        connId: this.connId,
                        config: this.activeRequestConfig,
                    })
            },
        },
    },
    beforeDestroy() {
        this.$typy(this.unwatch_stagingData).safeFunction()
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        ...mapActions({
            queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData',
        }),
        watch_stagingData() {
            this.unwatch_stagingData = this.$watch(
                'stagingData',
                data => {
                    const { immutableUpdate } = this.$helpers
                    const id = this.activeEntityId
                    const nodeMap = immutableUpdate(this.nodeMap, {
                        [id]: { data: { $set: data } },
                    })
                    ErdTask.update({ where: this.taskId, data: { nodeMap } })
                    ErdTask.dispatch('updateNodesHistory', nodeMap)
                    // Emit the event to update the node in the diagram
                    this.eventBus.$emit('entity-editor-ctr-update-node-data', { id, data })
                },
                { deep: true }
            )
        },
        close(formRef) {
            if (formRef.validate())
                ErdTaskTmp.update({
                    where: this.taskId,
                    data: { graph_height_pct: 100, active_entity_id: '' },
                })
            else
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('errors.requiredInputs')],
                    type: 'error',
                })
        },
    },
}
</script>
