<template>
    <div v-if="isAdmin">
        <v-btn
            width="160"
            outlined
            height="36"
            rounded
            class="text-capitalize px-8 font-weight-medium"
            depressed
            small
            color="primary"
            @click.native="onCreate"
        >
            + {{ $mxs_t('createNew') }}
        </v-btn>
        <mxs-dlg
            ref="baseDialog"
            v-model="isDlgOpened"
            :onSave="onSave"
            :title="`${$mxs_t('createANew')}...`"
            isDynamicWidth
            hasFormDivider
        >
            <template v-slot:body>
                <v-select
                    id="resource-select"
                    v-model="selectedObjType"
                    :items="Object.values(MXS_OBJ_TYPES)"
                    name="resourceName"
                    outlined
                    dense
                    :height="36"
                    class="mt-4 resource-select vuetify-input--override v-select--mariadb error--text__bottom"
                    :menu-props="{
                        contentClass: 'v-select--menu-mariadb',
                        bottom: true,
                        offsetY: true,
                    }"
                    hide-details
                    :rules="[
                        v => !!v || $mxs_t('errors.requiredInput', { inputName: 'This field' }),
                    ]"
                    required
                    @input="onChangeObjType"
                >
                    <template v-slot:item="{ item, on, attrs }">
                        <v-list-item class="text-capitalize" v-bind="attrs" v-on="on">
                            {{ $mxs_tc(item, 1) }}
                        </v-list-item>
                    </template>
                    <template v-slot:selection="{ item }">
                        <div class="text-capitalize v-select__selection v-select__selection--comma">
                            {{ $mxs_tc(item, 1) }}
                        </div>
                    </template>
                </v-select>
            </template>
            <template v-if="selectedObjType" v-slot:form-body>
                <!-- Use isDlgOpened as a key to force a rerender so that
                default values can be "fresh" -->
                <div :key="isDlgOpened">
                    <label class="field__label mxs-color-helper text-small-text d-block">
                        {{ $mxs_t('mxsObjLabelName', { type: $mxs_tc(selectedObjType, 1) }) }}
                    </label>
                    <v-text-field
                        id="id"
                        v-model="objId"
                        :rules="[v => validateResourceId(v)]"
                        name="id"
                        required
                        class="resource-id vuetify-input--override error--text__bottom"
                        dense
                        :height="36"
                        outlined
                        :placeholder="
                            $mxs_t('nameYour', { type: $mxs_tc(selectedObjType, 1).toLowerCase() })
                        "
                    />
                    <service-form-input
                        v-if="selectedObjType === MXS_OBJ_TYPES.SERVICES"
                        :ref="`form_${selectedObjType}`"
                        :allFilters="all_filters"
                        :defRoutingTargetItems="serviceDefRoutingTargetItems"
                        :defFilterItem="serviceDefFilterItems"
                        :moduleParamsProps="{ modules }"
                    />
                    <monitor-form-input
                        v-else-if="selectedObjType === MXS_OBJ_TYPES.MONITORS"
                        :ref="`form_${selectedObjType}`"
                        :allServers="all_servers"
                        :defaultItems="defRelationshipItems"
                        :moduleParamsProps="{ modules }"
                    />
                    <filter-form-input
                        v-else-if="selectedObjType === MXS_OBJ_TYPES.FILTERS"
                        :ref="`form_${selectedObjType}`"
                        :moduleParamsProps="{ modules }"
                    />
                    <listener-form-input
                        v-else-if="selectedObjType === MXS_OBJ_TYPES.LISTENERS"
                        :ref="`form_${selectedObjType}`"
                        :allServices="all_services"
                        :defaultItems="defRelationshipItems"
                        :moduleParamsProps="{
                            validate: $typy($refs, 'baseDialog.$refs.form.validate').safeFunction,
                            modules,
                        }"
                    />
                    <server-form-input
                        v-else-if="selectedObjType === MXS_OBJ_TYPES.SERVERS"
                        :ref="`form_${selectedObjType}`"
                        :allServices="all_services"
                        :allMonitors="all_monitors"
                        :defaultItems="defRelationshipItems"
                        :moduleParamsProps="{
                            validate: $typy($refs, 'baseDialog.$refs.form.validate').safeFunction,
                            modules,
                        }"
                        class="mt-4"
                    />
                </div>
            </template>
        </mxs-dlg>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapActions, mapGetters, mapState, mapMutations } from 'vuex'
import { MXS_OBJ_TYPES } from '@share/constants'

export default {
    name: 'create-mxs-obj',
    props: {
        defFormType: { type: String, default: '' },
        defRelationshipObj: { type: Object, default: () => {} },
    },
    data() {
        return {
            isDlgOpened: false,
            selectedObjType: '',
            objId: '',
            defRelationshipItems: [],
            serviceDefRoutingTargetItems: [],
            serviceDefFilterItems: [],
        }
    },
    computed: {
        ...mapState({
            form_type: 'form_type',
            all_filters: state => state.filter.all_filters,
            all_modules_map: state => state.maxscale.all_modules_map,
            all_obj_ids: state => state.maxscale.all_obj_ids,
            all_monitors: state => state.monitor.all_monitors,
            all_servers: state => state.server.all_servers,
            all_services: state => state.service.all_services,
        }),
        ...mapGetters({
            isAdmin: 'user/isAdmin',
            getMxsObjModules: 'maxscale/getMxsObjModules',
        }),
        modules() {
            return this.getMxsObjModules(this.selectedObjType)
        },
        defRelationshipObjType() {
            return this.$typy(this.defRelationshipObj, 'type').safeString
        },
    },
    watch: {
        // trigger open dialog since form_type is used to open dialog without clicking button in this component
        async form_type(val) {
            if (val) await this.onCreate()
        },
        async isDlgOpened(val) {
            if (val) {
                await this.fetchAllMxsObjIds()
                this.handleSetFormType()
            } else if (this.form_type) this.SET_FORM_TYPE(null) // clear form_type
        },
        async selectedObjType(v) {
            await this.onChangeObjType(v)
        },
        objId(val) {
            // add hyphens when ever input have whitespace
            this.objId = val ? val.split(' ').join('-') : val
        },
    },
    created() {
        this.MXS_OBJ_TYPES = MXS_OBJ_TYPES
    },
    methods: {
        ...mapMutations(['SET_SHOULD_REFRESH_RESOURCE', 'SET_FORM_TYPE']),
        ...mapActions({
            fetchAllMxsObjIds: 'maxscale/fetchAllMxsObjIds',
            createService: 'service/createService',
            createMonitor: 'monitor/createMonitor',
            createFilter: 'filter/createFilter',
            createListener: 'listener/createListener',
            createServer: 'server/createServer',
            fetchAllServices: 'service/fetchAllServices',
            fetchAllServers: 'server/fetchAllServers',
            fetchAllMonitors: 'monitor/fetchAllMonitors',
            fetchAllFilters: 'filter/fetchAllFilters',
            fetchAllListeners: 'listener/fetchAllListeners',
            fetchAllModules: 'maxscale/fetchAllModules',
        }),
        async onCreate() {
            // fetch data before open dlg
            if (this.$typy(this.all_modules_map).isEmptyObject) await this.fetchAllModules()
            this.isDlgOpened = true
        },
        /**
         *  global form_type state has higher priority. It is
         *  used to trigger opening form dialog without
         *  clicking the button in this component
         */
        handleSetFormType() {
            if (this.form_type) this.selectedObjType = this.form_type
            else if (this.defFormType) this.selectedObjType = this.defFormType
            else this.selectedObjType = this.MXS_OBJ_TYPES.SERVICES
        },
        /**
         * If current page is a detail page and have relationship object,
         * set default relationship items
         */
        async onChangeObjType(val) {
            const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = this.MXS_OBJ_TYPES
            switch (val) {
                case SERVICES:
                    await this.fetchAllServices()
                    await this.fetchAllFilters()
                    if (this.defRelationshipObjType === SERVERS)
                        this.serviceDefRoutingTargetItems = [this.defRelationshipObj]
                    else if (this.defRelationshipObjType === FILTERS)
                        this.serviceDefFilterItems = [this.defRelationshipObj]
                    break
                case SERVERS:
                    await this.fetchAllServers()
                    await this.fetchAllServices()
                    await this.fetchAllMonitors()
                    if (this.defRelationshipObjType === SERVICES)
                        this.defRelationshipItems = [this.defRelationshipObj]
                    else if (this.defRelationshipObjType === MONITORS)
                        this.defRelationshipItems = this.defRelationshipObj
                    break
                case MONITORS:
                    await this.fetchAllMonitors()
                    await this.fetchAllServers()
                    if (this.defRelationshipObjType === SERVERS)
                        this.defRelationshipItems = [this.defRelationshipObj]
                    break
                case FILTERS:
                    await this.fetchAllFilters()
                    break
                case LISTENERS: {
                    await this.fetchAllListeners()
                    await this.fetchAllServices()
                    if (this.defRelationshipObjType === SERVICES)
                        this.defRelationshipItems = this.defRelationshipObj
                    break
                }
            }
        },

        async onSave() {
            const form = this.$refs[`form_${this.selectedObjType}`]
            const { moduleId, parameters, relationships } = form.getValues()
            const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = this.MXS_OBJ_TYPES
            let payload = {
                id: this.objId,
                parameters,
                callback: this[
                    `fetchAll${this.$helpers.capitalizeFirstLetter(this.selectedObjType)}`
                ],
            }
            let actionName = ''
            switch (this.selectedObjType) {
                case SERVICES:
                case MONITORS:
                    {
                        payload.module = moduleId
                        payload.relationships = relationships
                        actionName =
                            this.selectedObjType === SERVICES ? 'createService' : 'createMonitor'
                    }
                    break
                case LISTENERS:
                case SERVERS:
                    payload.relationships = relationships
                    actionName =
                        this.selectedObjType === LISTENERS ? 'createListener' : 'createServer'
                    break
                case FILTERS:
                    payload.module = moduleId
                    actionName = 'createFilter'
                    break
            }
            await this[actionName](payload)
            this.reloadHandler()
        },

        reloadHandler() {
            if (this.defRelationshipItems) this.SET_SHOULD_REFRESH_RESOURCE(true)
        },

        validateResourceId(val) {
            if (!val) return this.$mxs_t('errors.requiredInput', { inputName: 'id' })
            else if (this.all_obj_ids.includes(val)) return this.$mxs_t('errors.duplicatedValue')
            return true
        },
    },
}
</script>

<style lang="scss" scoped>
.v-select--mariadb {
    ::v-deep .v-select__selection--comma {
        font-weight: bold;
    }
}
</style>
