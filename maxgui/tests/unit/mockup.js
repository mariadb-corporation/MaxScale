import { expect } from 'chai'

/**
 * This function mockups the selection of an item. By default, it finds all v-select
 * components and expect to have one v-select only, if selector param is defined, it finds component
 * using selector
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} selector Using class only. e.g. '.class'
 * @param {*} item item to be selected
 */
export async function mockupSelection(wrapper, item, selector = '') {
    let vSelects
    if (selector) {
        vSelects = wrapper.findAll(selector)
    } else {
        vSelects = wrapper.findAllComponents({ name: 'v-select' })
    }
    expect(vSelects.length).to.be.equal(1)
    await vSelects.at(0).vm.selectItem(item)
}

/**
 * This function mockups the change of an input field. By default, it finds all input
 * elements and expect to have one element only, if selector param is defined, it finds element
 * using selector
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} selector valid DOM selector syntax. e.g. '.class' or '#id'
 * @param {String} newValue new string value for input field
 */
export async function mockupInputChange(wrapper, newValue, selector = '') {
    let inputs
    if (selector) {
        inputs = wrapper.findAll(selector)
    } else {
        inputs = wrapper.findAll('input')
    }
    inputs.at(0).element.value = newValue
    expect(inputs.length).to.be.equal(1)
    // manually triggering input event on v-text-field
    await inputs.at(0).trigger('input')
}

/**
 * This function mockups the action of opening a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
export async function mockupOpenDialog(wrapper) {
    await wrapper.setProps({
        value: true,
    })
    expect(wrapper.vm.computeShowDialog).to.be.true
}

/**
 * This function mockups the action of closing a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
export async function mockupCloseDialog(wrapper) {
    await wrapper.setProps({
        value: false,
    })
    expect(wrapper.vm.computeShowDialog).to.be.false
}

/**
 * This function mockups the action of closing a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} path path to navigate to
 */
export async function mockupRouteChanges(wrapper, path) {
    if (wrapper.vm.$route.path !== path) await wrapper.vm.$router.push(path)
}

//a minimized mockup allModules data fetch from maxscale
export const allModules = [
    {
        attributes: {
            module_type: 'servers',
            parameters: [
                {
                    description: 'Server address',
                    mandatory: false,
                    modifiable: true,
                    name: 'address',
                    type: 'string',
                },
            ],
        },
        id: 'servers',
    },
    {
        attributes: {
            module_type: 'Filter',
            parameters: [
                {
                    mandatory: true,
                    name: 'inject',
                    type: 'quoted string',
                },
            ],
        },
        id: 'comment',
    },
    {
        attributes: {
            module_type: 'Authenticator',
            parameters: [],
        },
        id: 'MariaDBAuth',
    },
    {
        attributes: {
            module_type: 'Monitor',
            parameters: [
                {
                    default_value: '/cmapi/0.4.0',
                    mandatory: false,
                    name: 'admin_base_path',
                    type: 'string',
                },
            ],
        },
        id: 'csmon',
    },
    {
        attributes: {
            module_type: 'Router',
            parameters: [
                {
                    default_value: 'false',
                    mandatory: false,
                    name: 'delayed_retry',
                    type: 'bool',
                },
            ],
        },
        id: 'readwritesplit',
    },
    {
        attributes: {
            module_type: 'Protocol',
            parameters: [
                {
                    mandatory: true,
                    name: 'protocol',
                    type: 'string',
                },
            ],
            version: 'V1.1.0',
        },
        id: 'mariadbclient',
    },
]

//a minimized mockup allModulesMap state from vuex store
export const allModulesMap = {}
for (let i = 0; i < allModules.length; ++i) {
    const module = allModules[i]
    const moduleType = allModules[i].attributes.module_type
    if (allModulesMap[moduleType] == undefined) allModulesMap[moduleType] = []
    allModulesMap[moduleType].push(module)
}

export const mockupAllServers = [
    {
        attributes: {},
        id: 'row_server_0',
        links: {},
        relationships: {},
        type: 'servers',
    },
    {
        attributes: {},
        id: 'row_server_1',
        links: {},
        relationships: {},
        type: 'servers',
    },
]

export const mockupServersList = [
    {
        id: 'row_server_0',

        type: 'servers',
    },
    {
        id: 'row_server_1',

        type: 'servers',
    },
]

export const mockupAllFilters = [
    {
        attributes: {},
        id: 'filter_0',
        links: {},
        relationships: {},
        type: 'filters',
    },
    {
        attributes: {},
        id: 'filter_1',
        links: {},
        relationships: {},
        type: 'filters',
    },
]

export const mockupFiltersList = [
    {
        id: 'filter_0',

        type: 'filters',
    },
    {
        id: 'filter_1',

        type: 'filters',
    },
]

export const mockupAllServices = [
    {
        attributes: {},
        id: 'service_0',
        links: {},
        relationships: {},
        type: 'services',
    },
    {
        attributes: {},
        id: 'service_1',
        links: {},
        relationships: {},
        type: 'services',
    },
]

export const mockupServicesList = [
    {
        id: 'service_0',

        type: 'services',
    },
    {
        id: 'service_1',

        type: 'services',
    },
]

export const mockupAllMonitors = [
    {
        attributes: {},
        id: 'monitor_0',
        links: {},
        relationships: {},
        type: 'monitors',
    },
    {
        attributes: {},
        id: 'monitor_1',
        links: {},
        relationships: {},
        type: 'monitors',
    },
]

export const mockupMonitorsList = [
    {
        id: 'monitor_0',

        type: 'monitors',
    },
    {
        id: 'monitor_1',

        type: 'monitors',
    },
]
