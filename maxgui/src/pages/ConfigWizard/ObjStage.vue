<template>
    <v-form ref="formCtr" v-model="isFormValid" lazy-validation class="d-flex fill-height relative">
        <mxs-stage-ctr className="pa-0">
            <template v-slot:header>
                <div class="form--header pl-6 d-flex flex-column flex-grow-1">
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
                    <v-divider class="mt-3" />
                </div>
            </template>
            <template v-slot:body>
                <v-row class="fill-height">
                    <v-col cols="12" class="py-0">
                        <div class="form--body fill-height pl-6">
                            <server-form-input
                                v-if="objType === MXS_OBJ_TYPES.SERVERS"
                                ref="form"
                                :modules="modules"
                                :validate="validateForm"
                                :withRelationship="false"
                            />
                            <monitor-form-input
                                v-else-if="objType === MXS_OBJ_TYPES.MONITORS"
                                ref="form"
                                :modules="modules"
                                :allServers="allServers"
                                :defaultItems="getNewObjs(MXS_OBJ_TYPES.SERVERS)"
                            />
                            <filter-form-input
                                v-else-if="objType === MXS_OBJ_TYPES.FILTERS"
                                ref="form"
                                :modules="modules"
                            />
                            <service-form-input
                                v-else-if="objType === MXS_OBJ_TYPES.SERVICES"
                                ref="form"
                                :modules="modules"
                                :allFilters="allFilters"
                                :defRoutingTargetItems="getNewObjs(MXS_OBJ_TYPES.SERVERS)"
                                :defFilterItem="getNewObjs(MXS_OBJ_TYPES.FILTERS)"
                            />
                            <listener-form-input
                                v-else-if="objType === MXS_OBJ_TYPES.LISTENERS"
                                ref="form"
                                :validate="validateForm"
                                :modules="modules"
                                :allServices="allServices"
                                :defaultItems="
                                    $typy(getNewObjs(MXS_OBJ_TYPES.SERVICES), '[0]')
                                        .safeObjectOrEmpty
                                "
                            />
                        </div>
                    </v-col>
                </v-row>
            </template>
            <template v-slot:footer>
                <div class="pl-6 pt-4">
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
                </div>
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapState } from 'vuex'

export default {
    name: 'obj-stage',
    props: {
        objType: { type: String, required: true },
        stageDataMap: { type: Object, required: true }, // sync
    },
    data() {
        return {
            isFormValid: true,
            objId: '',
        }
    },
    computed: {
        ...mapState({ MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES }),
        ...mapGetters({ getMxsObjModules: 'maxscale/getMxsObjModules' }),
        modules() {
            return this.getMxsObjModules(this.objType)
        },
        dataMap: {
            get() {
                return this.stageDataMap
            },
            set(v) {
                this.$emit('stageDataMap:update', v)
            },
        },
        stageData: {
            get() {
                return this.dataMap[this.objType]
            },
            set(v) {
                this.dataMap[this.objType] = v
            },
        },
        existingIds() {
            return this.$typy(this.stageData, 'existingObjs').safeArray.map(obj => obj.id)
        },
        allServers() {
            return this.combineObjs(this.MXS_OBJ_TYPES.SERVERS)
        },
        allFilters() {
            return this.combineObjs(this.MXS_OBJ_TYPES.FILTERS)
        },
        allServices() {
            return this.combineObjs(this.MXS_OBJ_TYPES.SERVICES)
        },
        newObjs() {
            return this.getNewObjs(this.objType)
        },
        isNextDisabled() {
            return Boolean(!this.newObjs.length)
        },
    },
    watch: {
        objId(v) {
            this.objId = v ? v.split(' ').join('-') : v
        },
    },
    async created() {
        await this.fetchExistingObjData(this.objType)
    },
    methods: {
        ...mapActions({
            getResourceData: 'getResourceData',
            createService: 'service/createService',
            createMonitor: 'monitor/createMonitor',
            createFilter: 'filter/createFilter',
            createListener: 'listener/createListener',
            createServer: 'server/createServer',
        }),
        /**
         * @param {string} type object type
         * @returns {array} existing objects and recently added objects
         */
        combineObjs(type) {
            return [
                ...this.$typy(this.dataMap[type], 'existingObjs').safeArray,
                ...this.getNewObjs(type),
            ]
        },
        getNewObjs(type) {
            return this.$typy(this.dataMap[type], 'newObjs').safeArray
        },
        async fetchExistingObjData(type) {
            const { SERVERS, MONITORS } = this.MXS_OBJ_TYPES
            const relationshipFields = type === SERVERS ? [MONITORS] : []
            const res = await this.getResourceData({
                type,
                fields: ['id', ...relationshipFields],
            })
            this.$set(this.stageData, 'existingObjs', res)
        },
        validateObjId(v) {
            if (!v) return this.$mxs_t('errors.requiredInput', { inputName: 'id' })
            else if (this.existingIds.includes(v)) return this.$mxs_t('errors.duplicatedValue')
            return true
        },
        async validateForm() {
            await this.$typy(this.$refs, `formCtr.validate`).safeFunction()
        },
        resetForm() {
            this.$typy(this.$refs, `formCtr.reset`).safeFunction()
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
                this.stageData.newObjs.push({ id: payload.id, type: this.objType })
                this.resetForm()
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
