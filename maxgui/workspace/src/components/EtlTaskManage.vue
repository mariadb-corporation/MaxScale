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
import { mapState, mapGetters } from 'vuex'

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
        }),
        ...mapGetters({ hasErrAtCreation: 'etlMem/hasErrAtCreation' }),
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
        /**
         * @param {Object} task
         * @returns {Array} - etl actions
         */
        actions() {
            const types = Object.values(this.actionMap).filter(o => this.types.includes(o.type))
            const { CANCEL, DELETE, DISCONNECT, RESTART } = this.ETL_ACTIONS
            const status = this.task.status
            const { RUNNING } = this.ETL_STATUS
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
                        disabled =
                            status === RUNNING ||
                            EtlTask.getters('getEtlConnsByTaskId')(this.task.id).length === 0
                        break
                    case RESTART:
                        // hasErrAtCreation works for active etl task only
                        disabled = status === RUNNING || !this.hasErrAtCreation
                        break
                }
                return { ...o, disabled }
            })
        },
    },
    methods: {
        /**
         * @param {String} param.type - delete||cancel
         * @param {Object} param.task - task
         */
        async actionHandler(action) {
            const { CANCEL, DELETE, DISCONNECT, VIEW, RESTART } = this.ETL_ACTIONS
            switch (action.type) {
                case CANCEL:
                    await EtlTask.dispatch('cancelEtlTask', this.task.id)
                    break
                case DELETE:
                    await QueryConn.dispatch('disconnectConnsFromTask', this.task.id)
                    EtlTask.delete(this.task.id)
                    break
                case DISCONNECT:
                    await QueryConn.dispatch('disconnectConnsFromTask', this.task.id)
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
