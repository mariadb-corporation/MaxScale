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
                    v-model="selectedForm"
                    :items="Object.values(RESOURCE_FORM_TYPES)"
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
                    @input="handleFormSelection"
                />
            </template>
            <template v-if="selectedForm" v-slot:form-body>
                <!-- Use isDlgOpened as a key to force a rerender so that
                default values can be "fresh" -->
                <div :key="isDlgOpened">
                    <label class="field__label mxs-color-helper text-small-text d-block">
                        {{ $mxs_t('resourceLabelName', { resourceName: selectedForm }) }}
                    </label>
                    <v-text-field
                        id="id"
                        v-model="resourceId"
                        :rules="rules.resourceId"
                        name="id"
                        required
                        class="resource-id vuetify-input--override error--text__bottom"
                        dense
                        :height="36"
                        outlined
                        :placeholder="
                            $mxs_t('nameYour', { resourceName: selectedForm.toLowerCase() })
                        "
                    />
                    <service-form-input
                        v-if="selectedForm === RESOURCE_FORM_TYPES.SERVICE"
                        :ref="`form_${RESOURCE_FORM_TYPES.SERVICE}`"
                        :resourceModules="resourceModules"
                        :allFilters="all_filters"
                        :defaultItems="defaultRelationshipItems"
                    />
                    <monitor-form-input
                        v-else-if="selectedForm === RESOURCE_FORM_TYPES.MONITOR"
                        :ref="`form_${RESOURCE_FORM_TYPES.MONITOR}`"
                        :resourceModules="resourceModules"
                        :allServers="all_servers"
                        :defaultItems="defaultRelationshipItems"
                    />
                    <filter-form-input
                        v-else-if="selectedForm === RESOURCE_FORM_TYPES.FILTER"
                        :ref="`form_${RESOURCE_FORM_TYPES.FILTER}`"
                        :resourceModules="resourceModules"
                    />
                    <listener-form-input
                        v-else-if="selectedForm === RESOURCE_FORM_TYPES.LISTENER"
                        :ref="`form_${RESOURCE_FORM_TYPES.LISTENER}`"
                        :parentForm="$typy($refs, 'baseDialog.$refs.form').safeObjectOrEmpty"
                        :resourceModules="resourceModules"
                        :allServices="all_services"
                        :defaultItems="defaultRelationshipItems"
                    />
                    <server-form-input
                        v-else-if="selectedForm === RESOURCE_FORM_TYPES.SERVER"
                        :ref="`form_${RESOURCE_FORM_TYPES.SERVER}`"
                        :allServices="all_services"
                        :allMonitors="all_monitors"
                        :resourceModules="resourceModules"
                        :parentForm="$typy($refs, 'baseDialog.$refs.form').safeObjectOrEmpty"
                        :defaultItems="defaultRelationshipItems"
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapState, mapMutations } from 'vuex'
import ServiceFormInput from './ServiceFormInput'
import MonitorFormInput from './MonitorFormInput'
import FilterFormInput from './FilterFormInput'
import ListenerFormInput from './ListenerFormInput'
import ServerFormInput from './ServerFormInput'

export default {
    name: 'create-resource',
    components: {
        ServiceFormInput,
        MonitorFormInput,
        FilterFormInput,
        ListenerFormInput,
        ServerFormInput,
    },
    props: {
        defFormType: { type: String, default: '' },
        defRelationshipObj: { type: Object, default: () => {} },
    },
    data() {
        return {
            isDlgOpened: false,
            selectedForm: '',
            //COMMON
            resourceId: '', // resourceId is the name of resource being created
            rules: {
                resourceId: [val => this.validateResourceId(val)],
            },
            validateInfo: {},
            defaultRelationshipItems: [],
        }
    },
    computed: {
        ...mapState({
            RESOURCE_FORM_TYPES: state => state.app_config.RESOURCE_FORM_TYPES,
            RELATIONSHIP_TYPES: state => state.app_config.RELATIONSHIP_TYPES,
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
            getModulesByType: 'maxscale/getModulesByType',

            getAllServicesMap: 'service/getAllServicesMap',
            getAllServicesInfo: 'service/getAllServicesInfo',

            getAllServersInfo: 'server/getAllServersInfo',
            getAllServersMap: 'server/getAllServersMap',

            getAllMonitorsInfo: 'monitor/getAllMonitorsInfo',
            getAllMonitorsMap: 'monitor/getAllMonitorsMap',

            getAllFiltersInfo: 'filter/getAllFiltersInfo',

            getAllFiltersMap: 'filter/getAllFiltersMap',

            getAllListenersInfo: 'listener/getAllListenersInfo',
        }),
        resourceModules() {
            const { SERVICE, SERVER, MONITOR, LISTENER, FILTER } = this.RESOURCE_FORM_TYPES
            switch (this.selectedForm) {
                case SERVICE:
                    return this.getModulesByType('Router')
                case SERVER:
                    return this.getModulesByType('servers')
                case MONITOR:
                    return this.getModulesByType('Monitor')
                case FILTER:
                    return this.getModulesByType('Filter')
                case LISTENER: {
                    let authenticators = this.getModulesByType('Authenticator').map(item => item.id)
                    let protocols = this.getModulesByType('Protocol')
                    if (protocols.length) {
                        protocols.forEach(protocol => {
                            protocol.attributes.parameters = protocol.attributes.parameters.filter(
                                o => o.name !== 'protocol' && o.name !== 'service'
                            )
                            // Transform authenticator parameter from string type to enum type,
                            let authenticatorParamObj = protocol.attributes.parameters.find(
                                o => o.name === 'authenticator'
                            )
                            if (authenticatorParamObj) {
                                authenticatorParamObj.type = 'enum'
                                authenticatorParamObj.enum_values = authenticators
                                // add default_value for authenticator
                                authenticatorParamObj.default_value = ''
                            }
                        })
                    }
                    return protocols
                }
                default:
                    return []
            }
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
        async selectedForm(v) {
            await this.handleFormSelection(v)
        },
        resourceId(val) {
            // add hyphens when ever input have whitespace
            this.resourceId = val ? val.split(' ').join('-') : val
        },
    },

    methods: {
        ...mapMutations(['SET_REFRESH_RESOURCE', 'SET_FORM_TYPE']),
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
            if (this.form_type) this.selectedForm = this.form_type
            else if (this.defFormType) this.selectedForm = this.defFormType
            else this.selectedForm = this.RESOURCE_FORM_TYPES.SERVICE
        },
        async handleFormSelection(val) {
            const { SERVICE, SERVER, MONITOR, LISTENER, FILTER } = this.RESOURCE_FORM_TYPES
            const { SERVICES, SERVERS, MONITORS, FILTERS } = this.RELATIONSHIP_TYPES
            switch (val) {
                case SERVICE:
                    {
                        await this.fetchAllServices()
                        this.validateInfo = this.getAllServicesInfo
                        await this.fetchAllFilters()
                        this.setDefaultRelationship({
                            allResourcesMap: this.getAllServersMap,
                            relationshipType: SERVERS,
                            isMultiple: true,
                        })
                        this.setDefaultRelationship({
                            allResourcesMap: this.getAllFiltersMap,
                            relationshipType: FILTERS,
                            isMultiple: true,
                        })
                    }
                    break
                case SERVER:
                    await this.fetchAllServers()
                    this.validateInfo = this.getAllServersInfo
                    await this.fetchAllServices()
                    await this.fetchAllMonitors()
                    this.setDefaultRelationship({
                        allResourcesMap: this.getAllServicesMap,
                        relationshipType: SERVICES,
                        isMultiple: true,
                    })
                    this.setDefaultRelationship({
                        allResourcesMap: this.getAllMonitorsMap,
                        relationshipType: MONITORS,
                        isMultiple: false,
                    })
                    break
                case MONITOR:
                    await this.fetchAllMonitors()
                    this.validateInfo = this.getAllMonitorsInfo
                    await this.fetchAllServers()
                    this.setDefaultRelationship({
                        allResourcesMap: this.getAllServersMap,
                        relationshipType: SERVERS,
                        isMultiple: true,
                    })
                    break
                case FILTER:
                    await this.fetchAllFilters()
                    this.validateInfo = this.getAllFiltersInfo
                    break
                case LISTENER: {
                    await this.fetchAllListeners()
                    this.validateInfo = this.getAllListenersInfo
                    await this.fetchAllServices()
                    this.setDefaultRelationship({
                        allResourcesMap: this.getAllServicesMap,
                        relationshipType: SERVICES,
                        isMultiple: false,
                    })
                    break
                }
            }
        },
        /**
         * If current page is a detail page and have relationship object,
         * set default relationship item
         * @param {Map} payload.allResourcesMap - A Map object holds key-value in which key is the id of the resource
         * @param {String} payload.relationshipType - relationship type
         * @param {Boolean} payload.isMultiple - if relationship data allows multiple objects,
         * chosen items will be an array
         */
        setDefaultRelationship({ allResourcesMap, relationshipType, isMultiple }) {
            if (this.$typy(this.defRelationshipObj, 'type').safeString === relationshipType) {
                const objId = this.defRelationshipObj.id
                const { id = null, type = null } = allResourcesMap.get(objId) || {}
                if (id) this.defaultRelationshipItems = isMultiple ? [{ id, type }] : { id, type }
            }
        },

        async onSave() {
            const form = this.$refs[`form_${this.selectedForm}`]
            const { moduleId, parameters, relationships } = form.getValues()
            const { SERVICE, SERVER, MONITOR, LISTENER, FILTER } = this.RESOURCE_FORM_TYPES
            let payload = {
                id: this.resourceId,
                parameters,
                callback: this[`fetchAll${this.selectedForm}s`],
            }
            switch (this.selectedForm) {
                case SERVICE:
                case MONITOR:
                    {
                        payload.module = moduleId
                        payload.relationships = relationships
                    }
                    break
                case LISTENER:
                case SERVER:
                    payload.relationships = relationships
                    break
                case FILTER:
                    payload.module = moduleId
                    break
            }
            await this[`create${this.selectedForm}`](payload)
            this.reloadHandler()
        },

        reloadHandler() {
            if (this.defaultRelationshipItems) this.SET_REFRESH_RESOURCE(true)
        },

        validateResourceId(val) {
            if (!val) return this.$mxs_t('errors.requiredInput', { inputName: 'id' })
            else if (this.all_obj_ids.includes(val))
                return this.$mxs_t('errors.duplicatedValue', { inputValue: val })
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
