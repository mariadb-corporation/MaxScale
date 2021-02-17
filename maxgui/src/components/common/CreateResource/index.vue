<template>
    <div>
        <v-btn
            width="160"
            outlined
            height="36"
            rounded
            class="text-capitalize px-8 font-weight-medium"
            depressed
            small
            color="accent-dark"
            @click.native="onCreate"
        >
            + {{ $t('createNew') }}
        </v-btn>
        <base-dialog
            ref="baseDialog"
            v-model="isDialogOpen"
            :onSave="onSave"
            :title="`${$t('createANew')}...`"
            isDynamicWidth
        >
            <template v-slot:body>
                <v-select
                    id="resource-select"
                    v-model="selectedForm"
                    :items="formTypes"
                    name="resourceName"
                    outlined
                    dense
                    class="mt-4 resource-select std mariadb-select-input error--text__bottom"
                    :menu-props="{
                        contentClass: 'mariadb-select-v-menu',
                        bottom: true,
                        offsetY: true,
                    }"
                    hide-details
                    :rules="[v => !!v || $t('errors.requiredInput', { inputName: 'This field' })]"
                    required
                    @input="handleFormSelection"
                />
            </template>
            <template v-if="selectedForm" v-slot:form-body>
                <v-divider class="divider" />
                <div class="mb-0">
                    <label class="label color text-small-text d-block">
                        {{ $t('resourceLabelName', { resourceName: selectedForm }) }}
                    </label>
                    <v-text-field
                        id="id"
                        v-model="resourceId"
                        :rules="rules.resourceId"
                        name="id"
                        required
                        class="resource-id std error--text__bottom"
                        dense
                        outlined
                        :placeholder="$t('nameYour', { resourceName: selectedForm.toLowerCase() })"
                    />
                </div>

                <div v-if="selectedForm === 'Service'" class="mb-0">
                    <service-form-input
                        ref="formService"
                        :resourceModules="resourceModules"
                        :allServers="all_servers"
                        :allFilters="all_filters"
                        :defaultItems="defaultRelationshipItems"
                    />
                </div>
                <div v-else-if="selectedForm === 'Monitor'" class="mb-0">
                    <monitor-form-input
                        ref="formMonitor"
                        :resourceModules="resourceModules"
                        :allServers="all_servers"
                        :defaultItems="defaultRelationshipItems"
                    />
                </div>
                <div v-else-if="selectedForm === 'Filter'" class="mb-0">
                    <filter-form-input ref="formFilter" :resourceModules="resourceModules" />
                </div>
                <div v-else-if="selectedForm === 'Listener'" class="mb-0">
                    <listener-form-input
                        ref="formListener"
                        :parentForm="$refs.baseDialog.$refs.form || {}"
                        :resourceModules="resourceModules"
                        :allServices="all_services"
                        :defaultItems="defaultRelationshipItems"
                    />
                </div>
                <div v-else-if="selectedForm === 'Server'" class="mb-0">
                    <server-form-input
                        ref="formServer"
                        :allServices="all_services"
                        :allMonitors="all_monitors"
                        :resourceModules="resourceModules"
                        :parentForm="$refs.baseDialog.$refs.form || {}"
                        :defaultItems="defaultRelationshipItems"
                    />
                </div>
            </template>
        </base-dialog>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
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
    data: function() {
        return {
            isDialogOpen: false,
            selectedForm: '',
            formTypes: ['Service', 'Server', 'Monitor', 'Filter', 'Listener'],
            // module for monitor, service, and filter, listener
            resourceModules: [],
            //COMMON
            resourceId: '', // resourceId is the name of resource being created
            rules: {
                resourceId: [val => this.validateResourceId(val)],
            },
            validateInfo: {},
            // this is used to auto assign default selectedForm
            matchRoutes: [
                'monitor',
                'monitors',
                'server',
                'servers',
                'service',
                'services',
                'listener',
                'listeners',
                'filter',
                'filters',
            ],
            defaultRelationshipItems: null,
        }
    },

    computed: {
        ...mapState({
            form_type: 'form_type',
            all_filters: state => state.filter.all_filters,
            all_modules_map: state => state.maxscale.all_modules_map,
            all_monitors: state => state.monitor.all_monitors,
            all_servers: state => state.server.all_servers,
            all_services: state => state.service.all_services,
        }),
        ...mapGetters({
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
    },
    watch: {
        form_type: async function(val) {
            if (val) await this.onCreate()
            else if (this.form_type) this.SET_FORM_TYPE(null)
        },
        isDialogOpen: async function(val) {
            if (val) {
                // use route name to set default form
                if (!this.form_type) await this.setFormByRoute(this.$route.name)
                // use global form_type state to set default form
                else {
                    let formType = this.form_type.replace('FORM_', '') // remove FORM_ prefix
                    this.selectedForm = this.textTransform(formType)
                    await this.handleFormSelection(this.selectedForm)
                }
            } else {
                if (this.form_type) this.SET_FORM_TYPE(null)
                this.selectedForm = ''
            }
        },
        resourceId: function(val) {
            // add hyphens when ever input have whitespace
            this.resourceId = val ? val.split(' ').join('-') : val
        },
    },

    methods: {
        ...mapMutations(['SET_REFRESH_RESOURCE', 'SET_FORM_TYPE']),
        ...mapActions({
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
            await this.fetchAllModules()
            this.isDialogOpen = true
        },

        async handleFormSelection(val) {
            switch (val) {
                case 'Service':
                    {
                        this.resourceModules = this.getModuleType('Router')
                        await this.fetchAllServices()
                        this.validateInfo = this.getAllServicesInfo
                        await this.fetchAllServers()
                        await this.fetchAllFilters()
                        this.setDefaultRelationship({
                            allResourcesMap: this.getAllServersMap,
                            routeName: 'server',
                            isMultiple: true,
                        })
                        this.setDefaultRelationship({
                            allResourcesMap: this.getAllFiltersMap,
                            routeName: 'filter',
                            isMultiple: true,
                        })
                    }
                    break
                case 'Server':
                    this.resourceModules = this.getModuleType('servers')
                    await this.fetchAllServers()
                    this.validateInfo = this.getAllServersInfo
                    await this.fetchAllServices()
                    await this.fetchAllMonitors()
                    this.setDefaultRelationship({
                        allResourcesMap: this.getAllServicesMap,
                        routeName: 'service',
                        isMultiple: true,
                    })
                    this.setDefaultRelationship({
                        allResourcesMap: this.getAllMonitorsMap,
                        routeName: 'monitor',
                        isMultiple: false,
                    })
                    break
                case 'Monitor':
                    {
                        this.resourceModules = this.getModuleType('Monitor')
                        await this.fetchAllMonitors()
                        this.validateInfo = this.getAllMonitorsInfo
                        await this.fetchAllServers()
                        this.setDefaultRelationship({
                            allResourcesMap: this.getAllServersMap,
                            routeName: 'server',
                            isMultiple: true,
                        })
                    }
                    break
                case 'Filter':
                    this.resourceModules = this.getModuleType('Filter')
                    await this.fetchAllFilters()
                    this.validateInfo = this.getAllFiltersInfo
                    break
                case 'Listener':
                    {
                        let authenticators = this.getModuleType('Authenticator')
                        let authenticatorId = authenticators.map(item => `${item.id}`)
                        let protocols = this.getModuleType('Protocol')
                        if (protocols.length) {
                            protocols.forEach(protocol => {
                                // add default_value for protocol param
                                let protocolParamObj = protocol.attributes.parameters.find(
                                    o => o.name === 'protocol'
                                )
                                protocolParamObj.default_value = protocol.id
                                protocolParamObj.disabled = true
                                /*
                                    Transform authenticator parameter from string type to enum type,
                                 */
                                let authenticatorParamObj = protocol.attributes.parameters.find(
                                    o => o.name === 'authenticator'
                                )
                                if (authenticatorParamObj) {
                                    authenticatorParamObj.type = 'enum'
                                    authenticatorParamObj.enum_values = authenticatorId
                                    // add default_value for authenticator
                                    authenticatorParamObj.default_value = ''
                                }
                            })
                        }

                        this.resourceModules = protocols
                        await this.fetchAllListeners()
                        this.validateInfo = this.getAllListenersInfo
                        await this.fetchAllServices()
                        this.setDefaultRelationship({
                            allResourcesMap: this.getAllServicesMap,
                            routeName: 'service',
                            isMultiple: false,
                        })
                    }
                    break
            }
        },
        /**
         * If current page is a detail page and have relationship object,
         * set default relationship item
         * @param {Map} payload.allResourcesMap - A Map object holds key-value in which key is the id of the resource
         * @param {String} payload.routeName - route name of the details page: service, monitor, server, filter
         * @param {Boolean} payload.isMultiple - if relationship data allows multiple objects,
         * chosen items will be an array
         */
        setDefaultRelationship({ allResourcesMap, routeName, isMultiple }) {
            if (this.$route.name === routeName) {
                let currentResourceId = this.$route.params.id
                const { id = null, type = null } = allResourcesMap.get(currentResourceId) || {}
                if (id) this.defaultRelationshipItems = isMultiple ? [{ id, type }] : { id, type }
            }
        },

        /**
         * This function set default form based on route name
         * @param {String} routeName route name
         */
        async setFormByRoute(routeName) {
            if (this.matchRoutes.includes(routeName)) {
                this.selectedForm = this.textTransform(routeName)
                await this.handleFormSelection(this.selectedForm)
            } else {
                this.selectedForm = 'Service'
                await this.handleFormSelection('Service')
            }
        },

        /**
         * @param {String} str string to be processed
         * @return {String} return str that removed last char s and capitalized first char
         */
        textTransform(str) {
            let lowerCaseStr = str.toLowerCase()
            const suffix = 's'
            const chars = lowerCaseStr.split('')
            if (chars[chars.length - 1] === suffix) {
                lowerCaseStr = this.$help.strReplaceAt({
                    str: lowerCaseStr,
                    index: chars.length - 1,
                    newChar: '',
                })
            }
            return this.$help.capitalizeFirstLetter(lowerCaseStr)
        },

        getModuleType(type) {
            let allResourceModules = []
            if (this.all_modules_map[type]) allResourceModules = this.all_modules_map[type]
            return allResourceModules
        },

        async onSave() {
            const form = this.$refs[`form${this.selectedForm}`]
            const { moduleId, parameters, relationships } = form.getValues()

            let payload = {
                id: this.resourceId,
                parameters,
                callback: this[`fetchAll${this.selectedForm}s`],
            }
            switch (this.selectedForm) {
                case 'Service':
                case 'Monitor':
                    {
                        payload.module = moduleId
                        payload.relationships = relationships
                    }
                    break
                case 'Listener':
                case 'Server':
                    payload.relationships = relationships
                    break
                case 'Filter':
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
            const { idArr = [] } = this.validateInfo || {}
            if (!val) return this.$t('errors.requiredInput', { inputName: 'id' })
            else if (idArr.includes(val))
                return this.$t('errors.duplicatedValue', { inputValue: val })
            return true
        },
    },
}
</script>

<style lang="scss" scoped>
.divider {
    max-width: calc(100% + 124px);
    width: calc(100% + 124px);

    margin: 24px 0px 24px -62px;
}
.mariadb-select-input {
    ::v-deep .v-select__selection--comma {
        font-weight: bold;
    }
}
::v-deep .label {
    font-size: 0.625rem;
}
</style>
