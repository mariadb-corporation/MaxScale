<template>
    <details-page-title>
        <!-- Pass on all named slots -->
        <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
            <slot :name="slot" v-bind="props" />
        </template>
        <template v-slot:setting-menu>
            <v-list class="v-list--mariadb">
                <template v-for="(op, i) in monitorOps">
                    <v-divider v-if="op.divider" :key="`divider-${i}`" />
                    <v-subheader
                        v-else-if="op.subheader"
                        :key="op.subheader"
                        class="pl-2 pr-4 font-weight-medium op-subheader"
                    >
                        {{ op.subheader }}
                    </v-subheader>
                    <v-list-item
                        v-else
                        :key="op.text"
                        dense
                        link
                        :disabled="op.disabled"
                        class="px-2"
                        :class="`${op.type}-op`"
                        @click="onChooseOp(op)"
                    >
                        <v-list-item-title class="mxs-color-helper text-text">
                            <div class="d-inline-block text-center mr-2" style="width:24px">
                                <v-icon v-if="op.icon" :color="op.color" :size="op.iconSize">
                                    {{ op.icon }}
                                </v-icon>
                            </div>
                            {{ op.text }}
                        </v-list-item-title>
                    </v-list-item>
                </template>
            </v-list>
        </template>
        <template v-slot:append>
            <portal to="page-header--right">
                <div class="d-flex align-center fill-height">
                    <refresh-rate v-model="refreshRate" v-on="$listeners" />
                    <create-mxs-obj
                        class="ml-2 d-inline-block"
                        :defFormType="MXS_OBJ_TYPES.SERVERS"
                        :defRelationshipObj="{
                            id: $route.params.id,
                            type: MXS_OBJ_TYPES.MONITORS,
                        }"
                    />
                </div>
            </portal>
            <div>
                <status-icon
                    size="16"
                    class="monitor-state-icon mr-1"
                    :type="MXS_OBJ_TYPES.MONITORS"
                    :value="state"
                />
                <span class="resource-state mxs-color-helper text-navigation text-body-2">
                    {{ state }}
                </span>
                <span class="mxs-color-helper text-grayed-out text-body-2">
                    |
                    <span class="resource-module">{{ monitorModule }}</span>
                </span>
            </div>
            <mxs-conf-dlg
                v-model="confDlg.isOpened"
                :title="confDlg.title"
                :saveText="confDlgSaveTxt"
                :type="confDlg.type"
                :item="confDlg.targetNode"
                :smallInfo="confDlg.smallInfo"
                :onSave="onConfirm"
                minBodyWidth="500px"
            >
                <template v-slot:body-append>
                    <template
                        v-if="
                            confDlg.type === MONITOR_OP_TYPES.CS_REMOVE_NODE ||
                                confDlg.type === MONITOR_OP_TYPES.CS_ADD_NODE
                        "
                    >
                        <label class="field__label mxs-color-helper text-small-text label-required">
                            {{ $mxs_t('hostname/IP') }}
                        </label>
                        <v-combobox
                            v-model="confDlg.targetClusterNode"
                            :items="
                                confDlg.type === MONITOR_OP_TYPES.CS_REMOVE_NODE
                                    ? currCsNodeIds
                                    : all_server_names.filter(id => !currCsNodeIds.includes(id))
                            "
                            outlined
                            dense
                            class="vuetify-input--override v-select--mariadb error--text__bottom mb-3"
                            :menu-props="{
                                contentClass: 'v-select--menu-mariadb',
                                bottom: true,
                                offsetY: true,
                            }"
                            :placeholder="$mxs_t('selectNodeOrEnterIp')"
                            :height="36"
                            :rules="[
                                v =>
                                    !!v ||
                                    $mxs_t('errors.requiredInput', {
                                        inputName: $mxs_t('hostname/IP'),
                                    }),
                            ]"
                            hide-details="auto"
                        />
                    </template>
                    <template v-if="hasTimeout">
                        <duration-dropdown
                            :duration="confDlg.timeout"
                            :label="$mxs_t('timeout')"
                            :height="36"
                            :validationHandler="validateTimeout"
                            hide-details="auto"
                            required
                            @change="confDlg.timeout = $event"
                        />
                    </template>
                </template>
            </mxs-conf-dlg>
        </template>
    </details-page-title>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
/*
@chosen-op-type: type:String. Operation chosen type to dispatch update action
@on-count-done. Emit event after amount of time from <refresh-rate/>
@is-calling-op: boolean. Emit before and after opening dialog for calling ColumnStore command
*/
import { mapActions, mapMutations, mapState, mapGetters } from 'vuex'
import refreshRate from '@share/mixins/refreshRate'
import goBack from '@share/mixins/goBack'
import { MXS_OBJ_TYPES } from '@share/constants'
import { MONITOR_OP_TYPES, MRDB_MON } from '@rootSrc/constants'

export default {
    name: 'monitor-page-header',
    mixins: [refreshRate, goBack],
    props: {
        targetMonitor: { type: Object, required: true },
        successCb: { type: Function, required: true },
        shouldFetchCsStatus: { type: Boolean, default: false },
    },
    data() {
        return {
            // states for mxs-conf-dlg
            confDlg: {
                opType: '',
                isOpened: false,
                title: '',
                type: '',
                targetNode: null,
                smallInfo: '',
                timeout: '1m',
                targetClusterNode: null,
            },
        }
    },

    computed: {
        ...mapState({
            curr_cs_status: state => state.monitor.curr_cs_status,
            is_loading_cs_status: state => state.monitor.is_loading_cs_status,
            all_server_names: state => state.server.all_server_names,
        }),
        ...mapGetters({ getMonitorOps: 'monitor/getMonitorOps' }),
        confDlgSaveTxt() {
            const {
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_SET_READWRITE,
                CS_SET_READONLY,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
            } = this.MONITOR_OP_TYPES
            switch (this.confDlg.type) {
                case RESET_REP:
                    return 'reset'
                case RELEASE_LOCKS:
                    return 'release'
                case FAILOVER:
                    return 'perform'
                case CS_STOP_CLUSTER:
                    return 'stop'
                case CS_START_CLUSTER:
                    return 'start'
                case CS_SET_READWRITE:
                case CS_SET_READONLY:
                    return 'set'
                case CS_ADD_NODE:
                    return 'add'
                case CS_REMOVE_NODE:
                    return 'remove'
                default:
                    return this.confDlg.type
            }
        },
        state() {
            return this.$typy(this.targetMonitor, 'attributes.state').safeString
        },
        monitorModule() {
            return this.$typy(this.targetMonitor, 'attributes.module').safeString
        },
        isColumnStoreCluster() {
            return Boolean(
                this.$typy(this.targetMonitor, 'attributes.parameters.cs_admin_api_key').safeString
            )
        },
        hasTimeout() {
            const {
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_SET_READWRITE,
                CS_SET_READONLY,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
            } = this.MONITOR_OP_TYPES
            switch (this.confDlg.type) {
                case CS_STOP_CLUSTER:
                case CS_START_CLUSTER:
                case CS_SET_READWRITE:
                case CS_SET_READONLY:
                case CS_ADD_NODE:
                case CS_REMOVE_NODE:
                    return true
                default:
                    return false
            }
        },
        allOps() {
            return this.getMonitorOps({ currState: this.state, scope: this })
        },
        monitorOps() {
            const {
                attributes: { monitor_diagnostics: { primary = false } = {}, parameters = {} } = {},
            } = this.targetMonitor
            const {
                STOP,
                START,
                DESTROY,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_SET_READWRITE,
                CS_SET_READONLY,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
            } = this.MONITOR_OP_TYPES
            let ops = [this.allOps[STOP], this.allOps[START], this.allOps[DESTROY]]
            if (this.monitorModule === MRDB_MON) {
                ops = [...ops, { divider: true }, this.allOps[RESET_REP]]
                // only add the release_locks option when this cluster is a primary one
                if (primary) ops.push(this.allOps[RELEASE_LOCKS])
                // only add the failover option when auto_failover is false
                if (!this.$typy(parameters, 'auto_failover').safeBoolean)
                    ops.push(this.allOps[FAILOVER])
                // Add ColumnStore operations
                if (this.isColumnStoreCluster) {
                    ops = [
                        ...ops,
                        { divider: true },
                        { subheader: this.$mxs_t('csOps') },
                        {
                            ...this.allOps[CS_STOP_CLUSTER],
                            disabled: this.isClusterStopped,
                        },
                        {
                            ...this.allOps[CS_START_CLUSTER],
                            disabled: !this.isClusterStopped,
                        },
                        {
                            ...this.allOps[CS_SET_READONLY],
                            disabled: this.isClusterReadonly,
                        },
                        {
                            ...this.allOps[CS_SET_READWRITE],
                            disabled: !this.isClusterReadonly,
                        },
                        this.allOps[CS_ADD_NODE],
                        this.allOps[CS_REMOVE_NODE],
                    ]
                }
            }
            return ops
        },
        currCsNodesData() {
            let nodes = {}
            Object.keys(this.curr_cs_status).forEach(key => {
                const v = this.curr_cs_status[key]
                if (this.$typy(v).isObject) nodes[key] = v
            })
            return nodes
        },
        currCsNodeIds() {
            return Object.keys(this.currCsNodesData)
        },
        isClusterStopped() {
            return Object.values(this.currCsNodesData).every(v => v.services.length === 0)
        },
        isClusterReadonly() {
            return Object.values(this.currCsNodesData).every(v => v.cluster_mode === 'readonly')
        },
    },
    watch: {
        'confDlg.isOpened'(v) {
            if (v) this.$emit('is-calling-op', true)
            else {
                this.$emit('is-calling-op', false)
                // reset states to its initial state
                this.$nextTick(() => Object.assign(this.$data, this.$options.data.apply(this)))
            }
        },
        'targetMonitor.id': {
            immediate: true,
            async handler() {
                if (this.shouldFetchCsStatus) await this.fetchCsStatus()
            },
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
        this.MONITOR_OP_TYPES = MONITOR_OP_TYPES
    },
    methods: {
        ...mapMutations({ SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE' }),
        ...mapActions({
            manipulateMonitor: 'monitor/manipulateMonitor',
            handleFetchCsStatus: 'monitor/handleFetchCsStatus',
            fetchAllServerNames: 'server/fetchAllServerNames',
        }),
        async onChooseOp({ type, text, info, params }) {
            this.confDlg = {
                ...this.confDlg,
                type,
                opType: type,
                title: text,
                opParams: params,
                targetNode: { id: this.targetMonitor.id },
                smallInfo: info,
                isOpened: true,
            }
            if (type === this.MONITOR_OP_TYPES.CS_ADD_NODE) await this.fetchAllServerNames()
            this.$emit('chosen-op-type', type)
        },
        validateTimeout(v) {
            if (this.$typy(v).isEmptyString)
                return this.$mxs_t('errors.requiredInput', { inputName: this.$mxs_t('timeout') })
            else if (v <= 0)
                return this.$mxs_t('errors.largerThanZero', { inputName: this.$mxs_t('timeout') })
            return true
        },
        async fetchCsStatus() {
            await this.handleFetchCsStatus({
                monitorId: this.targetMonitor.id,
                monitorModule: this.monitorModule,
                isCsCluster: this.isColumnStoreCluster,
                monitorState: this.state,
                pollingResInterval: 1000,
            })
        },
        /**
         *
         * @param {String} param.type - operation type. Check MONITOR_OP_TYPES
         * @param {Object} param.meta - meta data.
         */
        validateRes({ type, meta }) {
            const {
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_SET_READWRITE,
                CS_SET_READONLY,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
            } = this.MONITOR_OP_TYPES
            switch (type) {
                case CS_ADD_NODE:
                case CS_REMOVE_NODE:
                    return (
                        (type === CS_REMOVE_NODE &&
                            !this.currCsNodesData[this.confDlg.targetClusterNode]) ||
                        (type === CS_ADD_NODE &&
                            this.currCsNodesData[this.confDlg.targetClusterNode])
                    )
                case CS_STOP_CLUSTER:
                case CS_START_CLUSTER:
                    return (
                        (type === CS_STOP_CLUSTER && this.isClusterStopped) ||
                        (type === CS_START_CLUSTER && !this.isClusterStopped)
                    )
                case CS_SET_READWRITE:
                case CS_SET_READONLY: {
                    return (
                        (type === CS_SET_READONLY && meta['cluster-mode'] === 'readonly') ||
                        (type === CS_SET_READWRITE && meta['cluster-mode'] === 'readwrite')
                    )
                }
            }
        },
        /**
         * @param {String} opType - operation type. Check MONITOR_OP_TYPES
         */
        csOpParamsCreator(opType) {
            const {
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
                CS_SET_READWRITE,
                CS_SET_READONLY,
            } = this.MONITOR_OP_TYPES
            switch (opType) {
                case CS_ADD_NODE:
                case CS_REMOVE_NODE:
                    return `&${this.confDlg.targetClusterNode}&${this.confDlg.timeout}`
                case CS_STOP_CLUSTER:
                case CS_START_CLUSTER:
                case CS_SET_READWRITE:
                case CS_SET_READONLY:
                    return `&${this.confDlg.timeout}`
            }
        },
        async onConfirm() {
            const {
                STOP,
                START,
                DESTROY,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_SET_READWRITE,
                CS_SET_READONLY,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
            } = this.MONITOR_OP_TYPES
            const type = this.confDlg.opType
            let payload = {
                id: this.targetMonitor.id,
                type,
                successCb: this.successCb,
            }
            switch (type) {
                case RESET_REP:
                case RELEASE_LOCKS:
                case FAILOVER: {
                    payload = {
                        ...payload,
                        opParams: { moduleType: this.monitorModule, params: '' },
                    }
                    break
                }
                case STOP:
                case START:
                    payload = { ...payload, opParams: this.confDlg.opParams }
                    break
                case DESTROY:
                    payload = { ...payload, successCb: this.goBack }
                    break
                case CS_STOP_CLUSTER:
                case CS_START_CLUSTER:
                case CS_SET_READWRITE:
                case CS_SET_READONLY:
                case CS_ADD_NODE:
                case CS_REMOVE_NODE:
                    payload = {
                        ...payload,
                        pollingResInterval: 1000,
                        custAsyncCmdDone: async meta => {
                            const action = this.$mxs_t(`monitorOps.actions.${payload.type}`)
                            await this.fetchCsStatus()
                            let msgs = [],
                                msgType = 'success'
                            if (this.validateRes({ type, meta })) msgs = [`${action} successfully`]
                            else {
                                msgs = [`Failed to ${action}`]
                                msgType = 'error'
                            }
                            this.SET_SNACK_BAR_MESSAGE({ text: msgs, type: msgType })
                            await this.successCb()
                        },
                        opParams: {
                            moduleType: this.monitorModule,
                            params: this.csOpParamsCreator(type),
                        },
                    }
                    break
            }
            await this.manipulateMonitor(payload)
        },
    },
}
</script>

<style lang="scss" scoped>
.op-subheader {
    height: 32px;
}
</style>
