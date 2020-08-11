<template>
    <base-dialog
        ref="baseDialog"
        v-model="computeShowDialog"
        :onCancel="closeModal"
        :onClose="closeModal"
        :onSave="handleSave"
        :title="`${$t('createANew')}...`"
        isDynamicWidth
    >
        <template v-if="selectedForm" v-slot:body>
            <v-select
                id="resource-select"
                v-model="selectedForm"
                :items="formTypes"
                name="resourceName"
                outlined
                dense
                class="resource-select std mariadb-select-input error--text__bottom"
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
                    ref="serviceForm"
                    :resourceModules="resourceModules"
                    :allServers="allServers"
                    :allFilters="allFilters"
                    :defaultItems="defaultRelationshipItems"
                />
            </div>
            <div v-else-if="selectedForm === 'Monitor'" class="mb-0">
                <monitor-form-input
                    ref="monitorForm"
                    :resourceModules="resourceModules"
                    :allServers="allServers"
                    :defaultItems="defaultRelationshipItems"
                />
            </div>
            <div v-else-if="selectedForm === 'Filter'" class="mb-0">
                <filter-form-input ref="filterForm" :resourceModules="resourceModules" />
            </div>
            <div v-else-if="selectedForm === 'Listener'" class="mb-0">
                <listener-form-input
                    ref="listenerForm"
                    :parentForm="$refs.baseDialog.$refs.form || {}"
                    :resourceModules="resourceModules"
                    :allServices="allServices"
                    :defaultItems="defaultRelationshipItems"
                />
            </div>
            <div v-else-if="selectedForm === 'Server'" class="mb-0">
                <server-form-input
                    ref="serverForm"
                    :allServices="allServices"
                    :allMonitors="allMonitors"
                    :resourceModules="resourceModules"
                    :parentForm="$refs.baseDialog.$refs.form || {}"
                    :defaultItems="defaultRelationshipItems"
                />
            </div>
        </template>
    </base-dialog>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapState } from 'vuex'
import ServiceFormInput from './ServiceFormInput'
import MonitorFormInput from './MonitorFormInput'
import FilterFormInput from './FilterFormInput'
import ListenerFormInput from './ListenerFormInput'
import ServerFormInput from './ServerFormInput'

export default {
    name: 'forms',
    components: {
        ServiceFormInput,
        MonitorFormInput,
        FilterFormInput,
        ListenerFormInput,
        ServerFormInput,
    },
    props: {
        value: Boolean,
        closeModal: Function,
    },
    data: function() {
        return {
            show: false,
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
            defaultRelationshipItems: {},
        }
    },

    computed: {
        ...mapState({
            form_type: 'form_type',
        }),
        ...mapGetters({
            allModulesMap: 'maxscale/allModulesMap',
            allServices: 'service/allServices',
            allServicesMap: 'service/allServicesMap',
            allServicesInfo: 'service/allServicesInfo',

            allServers: 'server/allServers',
            allServersInfo: 'server/allServersInfo',
            allServersMap: 'server/allServersMap',

            allMonitorsInfo: 'monitor/allMonitorsInfo',
            allMonitors: 'monitor/allMonitors',
            allMonitorsMap: 'monitor/allMonitorsMap',

            allFiltersInfo: 'filter/allFiltersInfo',
            allFilters: 'filter/allFilters',
            allFiltersMap: 'filter/allFiltersMap',

            allListenersInfo: 'listener/allListenersInfo',
        }),

        computeShowDialog: {
            // get value from props
            get() {
                return this.value
            },
            // set the value to show property in data
            set(value) {
                this.show = value
            },
        },
    },
    watch: {
        value: async function(val) {
            if (!val) return null
            else if (!this.form_type) await this.setDefaultForm(this.$route.name)
            else {
                let formType = this.form_type.replace('FORM_', '') // remove FORM_ prefix
                this.selectedForm = this.textTransform(formType)
                await this.handleFormSelection(this.selectedForm)
            }
        },
        resourceId: function(val) {
            // add hyphens when ever input have whitespace
            this.resourceId = val ? val.split(' ').join('-') : val
        },
    },

    methods: {
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
        }),

        async handleFormSelection(val) {
            const isMultiple = true // if relationship data allows multiple objects
            switch (val) {
                case 'Service':
                    {
                        this.resourceModules = this.getModuleType('Router')
                        await this.fetchAllServices()
                        this.validateInfo = this.allServicesInfo
                        await this.fetchAllServers()
                        await this.fetchAllFilters()
                        this.setDefaultRelationship(this.allServersMap, 'server', isMultiple)
                        this.setDefaultRelationship(this.allFiltersMap, 'filter', isMultiple)
                    }
                    break
                case 'Server':
                    this.resourceModules = this.getModuleType('servers')
                    await this.fetchAllServers()
                    this.validateInfo = this.allServersInfo
                    await this.fetchAllServices()
                    await this.fetchAllMonitors()
                    this.setDefaultRelationship(this.allServicesMap, 'service', isMultiple)
                    this.setDefaultRelationship(this.allMonitorsMap, 'monitor')
                    break
                case 'Monitor':
                    {
                        this.resourceModules = this.getModuleType('Monitor')
                        await this.fetchAllMonitors()
                        this.validateInfo = this.allMonitorsInfo
                        await this.fetchAllServers()
                        this.setDefaultRelationship(this.allServersMap, 'server', isMultiple)
                    }
                    break
                case 'Filter':
                    this.resourceModules = this.getModuleType('Filter')
                    await this.fetchAllFilters()
                    this.validateInfo = this.allFiltersInfo
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
                        this.validateInfo = this.allListenersInfo
                        await this.fetchAllServices()
                        this.setDefaultRelationship(this.allServicesMap, 'service')
                    }
                    break
            }
        },
        /**
         * If current page is a detail page and have relationship object,
         * set default relationship item
         * @param {Map} allResourcesMap A Map object holds key-value in which key is the id of the resource
         * @param {String} routeName route name of the details page: service, monitor, server, filter
         * @param {Boolean} isMultiple if relationship data allows multiple objects, chosen items will be an array
         *
         */
        setDefaultRelationship(allResourcesMap, routeName, isMultiple) {
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
        async setDefaultForm(routeName) {
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
            const arr = lowerCaseStr.split('')
            if (arr[arr.length - 1] === suffix) {
                lowerCaseStr = this.$help.strReplaceAt(lowerCaseStr, arr.length - 1, '')
            }
            let firstCharCapitalized = lowerCaseStr.charAt(0).toUpperCase()
            return firstCharCapitalized + lowerCaseStr.slice(1)
        },

        getModuleType(type) {
            let allResourceModules = []
            if (this.allModulesMap[type]) allResourceModules = this.allModulesMap[type]
            return allResourceModules
        },

        handleSave() {
            switch (this.selectedForm) {
                case 'Service':
                    {
                        const {
                            moduleId,
                            parameters,
                            relationships,
                        } = this.$refs.serviceForm.getValues()
                        const payload = {
                            id: this.resourceId,
                            router: moduleId,
                            parameters: parameters,
                            relationships: relationships,
                            callback: this.fetchAllServices,
                        }
                        this.createService(payload)
                    }
                    break
                case 'Monitor':
                    {
                        const {
                            moduleId,
                            parameters,
                            relationships,
                        } = this.$refs.monitorForm.getValues()
                        const payload = {
                            id: this.resourceId,
                            module: moduleId,
                            parameters: parameters,
                            relationships: relationships,
                            callback: this.fetchAllMonitors,
                        }
                        this.createMonitor(payload)
                    }
                    break
                case 'Filter':
                    {
                        const { moduleId, parameters } = this.$refs.filterForm.getValues()
                        const payload = {
                            id: this.resourceId,
                            module: moduleId,
                            parameters: parameters,
                            callback: this.fetchAllFilters,
                        }
                        this.createFilter(payload)
                    }
                    break
                case 'Listener':
                    {
                        const { parameters, relationships } = this.$refs.listenerForm.getValues()
                        const payload = {
                            id: this.resourceId,
                            parameters: parameters,
                            relationships: relationships,
                            callback: this.fetchAllListeners,
                        }
                        this.createListener(payload)
                    }
                    break
                case 'Server':
                    {
                        const { parameters, relationships } = this.$refs.serverForm.getValues()
                        const payload = {
                            id: this.resourceId,
                            parameters: parameters,
                            relationships: relationships,
                            callback: this.fetchAllServers,
                        }
                        this.createServer(payload)
                    }
                    break
            }
            this.closeModal()
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
