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

/**
 * This function finds and returns anchor link (<a> tag) in data-table.
 *
 * @param {Object} payload
 * @param {Object} payload.wrapper a mounted component and methods to test the component
 * @param {String} payload.rowId id of the row
 * @param {Number} payload.cellIndex index of cell that contains a tag
 * @returns {Object} mounted anchor tag element
 */
export function findAnchorLinkInTable({ wrapper, rowId, cellIndex }) {
    const dataTable = wrapper.findComponent({ name: 'data-table' })
    const tableCell = dataTable.find(`.cell-${cellIndex}-${rowId}`)
    const aTag = tableCell.find('a')
    return aTag
}

/**
 * This function returns unique resource names from table row at
 * a column
 * @param {Array} expectedTableRows
 * @param {String} colName column name
 * @returns {Array} unique resource names
 */
export function getUniqueResourceNames(expectedTableRows, colName) {
    let allNames = []
    expectedTableRows.forEach(row => {
        if (Array.isArray(row[colName])) {
            let name = row[colName].map(name => `${name}`)
            allNames.push(name)
        }
    })
    // create unique set then convert back to array with unique items
    const uniqueNames = [...new Set(allNames)]
    return uniqueNames
}

/**
 * This function triggers click event of a button
 * @param {Object} wrapper mounted component
 * @param {String} cssSelector css selector of the button to be clicked
 */
export async function triggerBtnClick(wrapper, cssSelector) {
    const btn = wrapper.find(`${cssSelector}`)
    await btn.trigger('click')
}

/**
 * This function opening confirm-dialog in page-header component
 * @param {Object} wrapper mounted component
 * @param {String} cssSelector css selector of the button to be clicked
 */
export async function openConfirmDialog(wrapper, cssSelector) {
    await triggerBtnClick(wrapper, '.gear-btn')
    const detailsIconGroupWrapper = wrapper.findComponent({
        name: 'details-icon-group-wrapper',
    })
    await triggerBtnClick(detailsIconGroupWrapper, cssSelector)
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

export const mockup_all_servers = [
    {
        attributes: { statistics: { connections: 0 } },
        id: 'row_server_0',
        links: {},
        relationships: {},
        type: 'servers',
    },
    {
        attributes: { statistics: { connections: 10 } },
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

export const mockup_all_filters = [
    {
        attributes: {
            module: 'qlafilter',
        },
        id: 'filter_0',
        links: {},
        relationships: {
            services: {
                data: [
                    {
                        id: 'RCR-Router',
                        type: 'services',
                    },
                    {
                        id: 'RCR-Writer',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'filters',
    },
    {
        attributes: {
            module: 'binlogfilter',
        },
        id: 'filter_1',
        links: {},
        relationships: {
            services: {
                data: [
                    {
                        id: 'RCR-Writer',
                        type: 'services',
                    },
                ],
            },
        },
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

export const mockup_all_services = [
    {
        attributes: {
            state: 'Started',
            router: 'readconnroute',
            connections: 0,
            total_connections: 1000001,
        },
        id: 'service_0',
        links: {},
        relationships: {
            servers: {
                data: [
                    {
                        id: 'row_server_0',
                        type: 'servers',
                    },
                ],
            },
        },
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
            router: 'readwritesplit',
            connections: 0,
            total_connections: 0,
        },
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
        attributes: { state: 'Running' },
        id: 'monitor_0',
        links: {},
        relationships: {},
        type: 'monitors',
    },
    {
        attributes: { state: 'Stopped' },
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

export const mockupGetAllMonitorsMap = new Map()
mockupAllMonitors.forEach(ele => {
    mockupGetAllMonitorsMap.set(ele.id, ele)
})

export const mockup_all_listeners = [
    {
        attributes: {
            parameters: {
                address: '::',
                protocol: 'mariadbclient',
                port: 3308,
            },
            state: 'Running',
        },
        id: 'RCR-Writer-Listener',
        relationships: {
            services: {
                data: [
                    {
                        id: 'RCR-Writer',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'listeners',
    },
    {
        attributes: {
            parameters: {
                address: '::',
                protocol: 'mariadbclient',
                port: 3306,
            },
            state: 'Running',
        },
        id: 'RWS-Listener',
        relationships: {
            services: {
                data: [
                    {
                        id: 'RWS-Router',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'listeners',
    },
    {
        attributes: {
            parameters: {
                address: '::',
                protocol: 'mariadbclient',
                port: null,
                socket: '/tmp/maxscale.sock',
            },
            state: 'Running',
        },
        id: 'RCR-Router-Listener',
        relationships: {
            services: {
                data: [
                    {
                        id: 'RCR-Router',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'listeners',
    },
]

export const serviceStateTableRows = mockupServicesList.map(service => ({
    ...service,
    state: 'Started',
}))

export const allServicesState = [
    {
        attributes: {
            state: 'Started',
        },
        id: 'service_0',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'service_1',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'RWS-Router',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'RCR-Router',
        type: 'services',
    },
    {
        attributes: {
            state: 'Started',
        },
        id: 'RCR-Writer',
        type: 'services',
    },
]

export const mockup_all_sessions = [
    {
        attributes: {
            connected: 'Thu Aug 13 14:06:17 2020',
            idle: 55.5,
            remote: '::ffff:127.0.0.1',
            user: 'maxskysql',
        },
        id: '1000002',

        relationships: {
            services: {
                data: [
                    {
                        id: 'RCR-Router',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'sessions',
    },
]

export const mockup_maxscale_module_parameters = [
    {
        default_value: true,
        description: 'Admin interface authentication.',
        mandatory: false,
        modifiable: false,
        name: 'admin_auth',
        type: 'bool',
    },
    {
        default_value: {
            count: 0,
            suppress: 0,
            window: 0,
        },
        description: `Limit the amount of identical log messages than can
        be logged during a certain time period.`,
        mandatory: false,
        modifiable: true,
        name: 'log_throttling',
        type: 'throttling',
    },
    {
        default_value: false,
        description: 'Log a warning when a user with super privilege logs in.',
        mandatory: false,
        modifiable: false,
        name: 'log_warn_super_user',
        type: 'bool',
    },
]
export const mockup_maxscale_parameters = {
    admin_auth: true,
    log_throttling: {
        count: 0,
        suppress: 0,
        window: 0,
    },
    log_warn_super_user: false,
}
export const processedMaxScaleModuleParameters = [
    {
        default_value: true,
        description: 'Admin interface authentication.',
        mandatory: false,
        modifiable: false,
        name: 'admin_auth',
        type: 'bool',
    },
    {
        default_value: {
            count: 0,
            suppress: 0,
            window: 0,
        },
        description: `Limit the amount of identical log messages than can
        be logged during a certain time period.`,
        mandatory: false,
        modifiable: true,
        name: 'log_throttling',
        type: 'throttling',
    },
    {
        name: 'count',
        type: 'count',
        modifiable: true,
        default_value: 0,
        description: 'Positive integer specifying the number of logged times',
    },
    {
        name: 'suppress',
        type: 'duration',
        modifiable: true,
        unit: 'ms',
        default_value: 0,
        description: 'The suppressed duration before the logging of a particular error',
    },
    {
        name: 'window',
        type: 'duration',
        modifiable: true,
        unit: 'ms',
        default_value: 0,
        description: 'The duration that a particular error may be logged',
    },
    {
        default_value: false,
        description: 'Log a warning when a user with super privilege logs in.',
        mandatory: false,
        modifiable: false,
        name: 'log_warn_super_user',
        type: 'bool',
    },
]
