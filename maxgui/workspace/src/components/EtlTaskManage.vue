<template>
    <v-menu transition="slide-y-transition" offset-y left v-bind="{ ...$attrs }" v-on="$listeners">
        <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
            <slot :name="slot" v-bind="slotData" />
        </template>
        <v-list>
            <v-list-item
                v-for="action in actions"
                :key="action.text"
                :disabled="action.disabled"
                @click="actionHandler(action)"
            >
                <v-list-item-title
                    class="mxs-color-helper"
                    :class="[action.type === ETL_ACTIONS.DELETE ? 'text-error' : 'text-text']"
                >
                    {{ action.text }}
                </v-list-item-title>
            </v-list-item>
        </v-list>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Emit:
 * @on-restart: string : etl task id
 */
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'etl-task-manage',
    inheritAttrs: false,
    props: {
        id: { type: String, required: true },
        types: { type: Array, required: true },
    },
    computed: {
        ...mapState({
            ETL_ACTIONS: state => state.mxsWorkspace.config.ETL_ACTIONS,
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
            MIGR_DLG_TYPES: state => state.mxsWorkspace.config.MIGR_DLG_TYPES,
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
        }),
        actionMap() {
            return Object.keys(this.ETL_ACTIONS).reduce((obj, key) => {
                const value = this.ETL_ACTIONS[key]
                obj[value] = {
                    text: this.$mxs_t(`etlOps.actions.${value}`),
                    type: value,
                }
                return obj
            }, {})
        },
        task() {
            return (
                EtlTask.query()
                    .whereId(this.id)
                    .with('connections')
                    .first() || {}
            )
        },
        hasNoConn() {
            return EtlTask.getters('getEtlConnsByTaskId')(this.task.id).length === 0
        },
        /**
         * @param {Object} task
         * @returns {Array} - etl actions
         */
        actions() {
            const types = Object.values(this.actionMap).filter(o => this.types.includes(o.type))
            const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART } = this.ETL_ACTIONS
            const status = this.task.status
            const { INITIALIZING, RUNNING, COMPLETE } = this.ETL_STATUS
            return types.map(o => {
                let disabled = false
                switch (o.type) {
                    case CANCEL:
                        if (status !== RUNNING) disabled = true
                        break
                    case DELETE:
                        if (status === RUNNING) disabled = true
                        break
                    case DISCONNECT:
                        disabled = status === RUNNING || this.hasNoConn
                        break
                    case MIGR_OTHER_OBJS:
                        disabled = status === RUNNING || this.hasNoConn
                        break
                    case RESTART:
                        disabled =
                            status === RUNNING || status === COMPLETE || status === INITIALIZING
                        break
                }
                return { ...o, disabled }
            })
        },
    },
    methods: {
        ...mapMutations({ SET_MIGR_DLG: 'mxsWorkspace/SET_MIGR_DLG' }),
        /**
         * @param {String} param.type - delete||cancel
         * @param {Object} param.task - task
         */
        async actionHandler(action) {
            const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, VIEW, RESTART } = this.ETL_ACTIONS
            const { SRC_OBJ } = this.ETL_STAGE_INDEX
            switch (action.type) {
                case CANCEL:
                    await EtlTask.dispatch('cancelEtlTask', this.task.id)
                    break
                case DELETE:
                    this.SET_MIGR_DLG({
                        etl_task_id: this.task.id,
                        type: this.MIGR_DLG_TYPES.DELETE,
                        is_opened: true,
                    })
                    break
                case DISCONNECT:
                    await QueryConn.dispatch('disconnectConnsFromTask', this.task.id)
                    break
                case MIGR_OTHER_OBJS:
                    EtlTask.update({
                        where: this.task.id,
                        data(obj) {
                            obj.active_stage_index = SRC_OBJ
                        },
                    })
                    break
                case VIEW:
                    EtlTask.dispatch('viewEtlTask', this.task)
                    break
                case RESTART:
                    this.$emit('on-restart', this.task.id)
                    break
            }
        },
    },
}
</script>
