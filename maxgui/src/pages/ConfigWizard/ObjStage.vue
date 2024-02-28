<template>
    <v-form ref="formCtr" v-model="isFormValid" lazy-validation class="fill-height">
        <mxs-stage-ctr
            className="pa-0"
            headerClassName="pl-9 pt-1"
            bodyClassName="pl-9 fill-height"
            footerClassName="pl-9 pt-4"
        >
            <template v-slot:header>
                <div class="form--header d-flex flex-column flex-grow-1">
                    <label
                        class="d-block field__label mxs-color-helper text-small-text label-required"
                    >
                        {{ $mxs_t('mxsObjLabelName', { type: $mxs_tc(objType, 1) }) }}
                    </label>
                    <v-text-field
                        id="id"
                        :key="objType"
                        v-model="objId"
                        :rules="[v => validateObjId(v)]"
                        name="id"
                        required
                        class="obj-id vuetify-input--override error--text__bottom"
                        dense
                        :height="36"
                        outlined
                        :placeholder="
                            $mxs_t('nameYour', {
                                type: $mxs_tc(objType, 1).toLowerCase(),
                            })
                        "
                    />
                    <v-divider class="my-3" />
                </div>
            </template>
            <template v-slot:body>
                <server-form-input
                    v-if="objType === MXS_OBJ_TYPES.SERVERS"
                    ref="form"
                    :withRelationship="false"
                    :moduleParamsProps="{
                        ...moduleParamsProps,
                        showAdvanceToggle: true,
                        validate: validateForm,
                    }"
                />
                <monitor-form-input
                    v-else-if="objType === MXS_OBJ_TYPES.MONITORS"
                    ref="form"
                    :allServers="allServers"
                    :defaultItems="getNewObjsByType(MXS_OBJ_TYPES.SERVERS)"
                    :moduleParamsProps="moduleParamsProps"
                />
                <filter-form-input
                    v-else-if="objType === MXS_OBJ_TYPES.FILTERS"
                    ref="form"
                    :moduleParamsProps="moduleParamsProps"
                />
                <service-form-input
                    v-else-if="objType === MXS_OBJ_TYPES.SERVICES"
                    ref="form"
                    :allFilters="allFilters"
                    :defRoutingTargetItems="getNewObjsByType(MXS_OBJ_TYPES.SERVERS)"
                    :defFilterItem="getNewObjsByType(MXS_OBJ_TYPES.FILTERS)"
                    :moduleParamsProps="moduleParamsProps"
                />
                <listener-form-input
                    v-else-if="objType === MXS_OBJ_TYPES.LISTENERS"
                    ref="form"
                    :allServices="allServices"
                    :defaultItems="
                        $typy(getNewObjsByType(MXS_OBJ_TYPES.SERVICES), '[0]').safeObjectOrEmpty
                    "
                    :moduleParamsProps="{
                        ...moduleParamsProps,
                        showAdvanceToggle: true,
                        validate: validateForm,
                    }"
                />
            </template>
            <template v-slot:footer>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="mt-auto mr-2 font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    :outlined="!isNextDisabled"
                    :disabled="!isFormValid"
                    @click="handleCreate"
                >
                    {{ $mxs_t('createObj') }}
                </v-btn>
                <v-btn
                    v-if="objType !== MXS_OBJ_TYPES.LISTENERS"
                    small
                    height="36"
                    color="primary"
                    class="mt-auto font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    :outlined="isNextDisabled"
                    :disabled="isNextDisabled"
                    @click="$emit('next')"
                >
                    {{ $mxs_t('next') }}
                </v-btn>
                <v-btn
                    v-else-if="!isNextDisabled"
                    small
                    height="36"
                    color="primary"
                    class="mt-auto font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    to="/visualization/configuration"
                >
                    {{ $mxs_t('visualizeConfig') }}
                </v-btn>
            </template>
        </mxs-stage-ctr>
    </v-form>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapState } from 'vuex'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'obj-stage',
    props: {
        objType: { type: String, required: true },
        stageDataMap: { type: Object, required: true },
    },
    data() {
        return {
            isFormValid: true,
            objId: '',
        }
    },
    computed: {
        ...mapState({ search_keyword: 'search_keyword' }),
        ...mapGetters({ getMxsObjModules: 'maxscale/getMxsObjModules' }),
        modules() {
            return this.getMxsObjModules(this.objType)
        },
        existingIds() {
            return Object.keys(this.stageDataMap).flatMap(type =>
                this.getAllObjsByType(type).map(obj => obj.id)
            )
        },
        allServers() {
            return this.getAllObjsByType(this.MXS_OBJ_TYPES.SERVERS)
        },
        allFilters() {
            return this.getAllObjsByType(this.MXS_OBJ_TYPES.FILTERS)
        },
        allServices() {
            return this.getAllObjsByType(this.MXS_OBJ_TYPES.SERVICES)
        },
        newObjs() {
            return this.getNewObjsByType(this.objType)
        },
        isNextDisabled() {
            if (this.objType === this.MXS_OBJ_TYPES.FILTERS) return false
            return Boolean(!this.newObjs.length)
        },
        moduleParamsProps() {
            return {
                showAdvanceToggle: true,
                modules: this.modules,
                search: this.search_keyword,
            }
        },
    },
    watch: {
        objId(v) {
            this.objId = v ? v.split(' ').join('-') : v
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        ...mapActions({
            createService: 'service/createService',
            createMonitor: 'monitor/createMonitor',
            createFilter: 'filter/createFilter',
            createListener: 'listener/createListener',
            createServer: 'server/createServer',
        }),
        /**
         * @param {string} param.type - stage type
         * @param {string} param.field - field name. i.e. newObjMap or existingObjMap
         * @returns {object} either newObjMap or existingObjMap
         */
        getObjMap({ type, field }) {
            return this.$typy(this.stageDataMap[type], field).safeObjectOrEmpty
        },
        /**
         * @param {string} type object type
         * @returns {array} existing objects and recently added objects
         */
        getAllObjsByType(type) {
            const objMap = this.$helpers.lodash.merge(
                this.getObjMap({ type, field: 'existingObjMap' }),
                this.getObjMap({ type, field: 'newObjMap' })
            )
            return Object.values(objMap)
        },
        /**
         * @param {string} type object type
         * @returns {array} recently added objects
         */
        getNewObjsByType(type) {
            return Object.values(
                this.getObjMap({ type, field: 'newObjMap' })
            ).map(({ id, type }) => ({ id, type }))
        },
        validateObjId(v) {
            if (!v) return this.$mxs_t('errors.requiredInput', { inputName: 'id' })
            else if (this.existingIds.includes(v)) return this.$mxs_t('errors.duplicatedValue')
            return true
        },
        async validateForm() {
            await this.$typy(this.$refs, `formCtr.validate`).safeFunction()
        },
        emptyObjId() {
            this.objId = ''
            this.$typy(this.$refs, `formCtr.resetValidation`).safeFunction()
        },
        async createObj() {
            const form = this.$refs.form
            const { moduleId, parameters = {}, relationships = {} } = form.getValues()
            const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = this.MXS_OBJ_TYPES
            let payload = {
                id: this.objId,
                parameters,
            }
            let actionName = ''
            switch (this.objType) {
                case SERVERS:
                    actionName = 'createServer'
                    break
                case MONITORS:
                case SERVICES:
                    payload.module = moduleId
                    payload.relationships = relationships
                    actionName = this.objType === SERVICES ? 'createService' : 'createMonitor'
                    break
                case FILTERS:
                    payload.module = moduleId
                    actionName = 'createFilter'
                    break
                case LISTENERS:
                    payload.relationships = relationships
                    actionName = 'createListener'
                    break
            }
            payload.callback = () => {
                this.emptyObjId()
                this.$emit('on-obj-created', { id: payload.id, type: this.objType })
            }
            await this[actionName](payload)
        },
        async handleCreate() {
            await this.validateForm()
            if (!this.isFormValid) this.$helpers.scrollToFirstErrMsgInput()
            else await this.createObj()
        },
    },
}
</script>
