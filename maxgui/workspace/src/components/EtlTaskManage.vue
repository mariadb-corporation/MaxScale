<template>
    <v-menu
        transition="slide-y-transition"
        offset-y
        left
        content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-for="slot in Object.keys($scopedSlots)" v-slot:[slot]="slotData">
            <slot :name="slot" v-bind="slotData" />
        </template>
        <v-list>
            <v-list-item
                v-for="action in actions"
                :key="action.text"
                :disabled="action.disabled"
                @click="handler(action.type)"
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
 * Change Date: 2028-04-03
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
import { ETL_ACTIONS, ETL_STATUS } from '@wsSrc/constants'

export default {
    name: 'etl-task-manage',
    inheritAttrs: false,
    props: {
        task: { type: Object, required: true },
        types: { type: Array, required: true },
    },
    computed: {
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
        hasNoConn() {
            return QueryConn.getters('findEtlConns')(this.task.id).length === 0
        },
        isRunning() {
            return this.task.status === ETL_STATUS.RUNNING
        },
        /**
         * @param {Object} task
         * @returns {Array} - etl actions
         */
        actions() {
            const types = Object.values(this.actionMap).filter(o => this.types.includes(o.type))
            const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, RESTART } = this.ETL_ACTIONS
            const status = this.task.status
            const { INITIALIZING, COMPLETE } = ETL_STATUS
            return types.map(o => {
                let disabled = false
                switch (o.type) {
                    case CANCEL:
                        disabled = !this.isRunning
                        break
                    case DELETE:
                        disabled = this.isRunning
                        break
                    case DISCONNECT:
                        disabled = this.isRunning || this.hasNoConn
                        break
                    case MIGR_OTHER_OBJS:
                        disabled = this.isRunning || this.hasNoConn
                        break
                    case RESTART:
                        disabled = this.isRunning || status === COMPLETE || status === INITIALIZING
                        break
                }
                return { ...o, disabled }
            })
        },
    },
    created() {
        this.ETL_ACTIONS = ETL_ACTIONS
    },
    methods: {
        async handler(type) {
            if (type === this.ETL_ACTIONS.RESTART) this.$emit('on-restart', this.task.id)
            else await EtlTask.dispatch('actionHandler', { type, task: this.task })
        },
    },
}
</script>
