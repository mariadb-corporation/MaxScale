/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { expect } from 'chai'

/**
 * This function mockups the selection of an item. By default, it finds all v-select
 * components and expect to have one v-select only, if selector param is defined, it finds component
 * using selector
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} selector Using class only. e.g. '.class'
 * @param {*} item item to be selected
 */
export async function itemSelectMock(wrapper, item, selector = '') {
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
export async function inputChangeMock(wrapper, newValue, selector = '') {
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
 * This function mockups the action of closing a dialog
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 */
export async function hideDialogMock(wrapper) {
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
export async function routeChangesMock(wrapper, path) {
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
export function getUniqueResourceNamesStub(expectedTableRows, colName) {
    let allNames = []
    expectedTableRows.forEach(row => {
        if (Array.isArray(row[colName])) {
            let names = row[colName].map(name => `${name}`)
            allNames = [...allNames, ...names]
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
 * @param {Object} payload.wrapper mounted component
 * @param {String} payload.cssSelector css selector of the button to be clicked
 */
export async function openConfirmDialog({ wrapper, cssSelector }) {
    await triggerBtnClick(wrapper, '.gear-btn')
    await triggerBtnClick(wrapper, cssSelector)
}

/**
 * This function asserts if request is sent with accurate endpoint and payload when
 * clicking save btn of confirm-dialog in page-header component
 * @param {Object} payload.wrapper - mounted component
 * @param {String} payload.cssSelector - css selector of the button to be clicked
 * @param {Object} payload.axiosStub - axiosStub
 * @param {String} payload.axiosStubCalledWith - argument for calledWith method
 */
export async function assertSendingRequest({
    wrapper,
    cssSelector,
    axiosStub,
    axiosStubCalledWith,
}) {
    await openConfirmDialog({
        wrapper,
        cssSelector,
    })
    const confirmDialog = wrapper.findComponent({
        name: 'confirm-dialog',
    })
    await triggerBtnClick(confirmDialog, '.save')
    await axiosStub.should.have.been.calledWith(axiosStubCalledWith)
}

/**
 * This function tests if PATCH request is sent with accurate
 * endpoint and payload data. Notice: mounted component must have
 * dispatchRelationshipUpdate method
 * @param {Object} payload.wrapper mounted component
 * @param {Object} payload.currentResource target resource to be updated
 * @param {Object} payload.axiosPatchStub axios stub to be tested
 * @param {String} payload.relationshipType type of relationship being updated
 */
export async function testRelationshipUpdate({
    wrapper,
    currentResource,
    axiosPatchStub,
    relationshipType,
}) {
    const {
        id: currentResourceId,
        type: currentResourceType,
        relationships: {
            [relationshipType]: { data: currentData },
        },
    } = currentResource

    const dataStub = [...currentData, { id: 'test', relationshipType }]

    await wrapper.vm.dispatchRelationshipUpdate({ type: relationshipType, data: dataStub })

    await axiosPatchStub.should.have.been.calledWith(
        `/${currentResourceType}/${currentResourceId}/relationships/${relationshipType}`,
        {
            data: dataStub,
        }
    )
}

const dummy_all_modules = [
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

export const all_modules_map_stub = {}

dummy_all_modules.forEach(fake_module => {
    const module = fake_module
    const moduleType = fake_module.attributes.module_type
    if (all_modules_map_stub[moduleType] == undefined) all_modules_map_stub[moduleType] = []
    all_modules_map_stub[moduleType].push(module)
})

export const dummy_all_servers = [
    {
        attributes: {
            last_event: 'master_up',
            triggered_at: 'Fri, 21 Aug 2020 06:04:40 GMT',
            version_string: '10.4.12-MariaDB-1:10.4.12+maria~bionic-log',
            state: 'Master, Running',
            statistics: { connections: 0 },
        },
        id: 'row_server_0',
        links: {},
        relationships: {
            monitors: {
                data: [
                    {
                        id: 'monitor_0',
                        type: 'monitors',
                    },
                ],
            },
            services: {
                data: [
                    {
                        id: 'service_0',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'servers',
    },
    {
        attributes: { state: 'Slave, Running', statistics: { connections: 10 } },
        id: 'row_server_1',
        links: {},
        relationships: {},
        type: 'servers',
    },
]

export const getUnMonitoredServersStub = () => {
    let unMonitoredServers = []

    dummy_all_servers.forEach(({ id, type, relationships: { monitors = null } = {} }) => {
        if (!monitors) unMonitoredServers.push({ id, type })
    })
    return unMonitoredServers
}

export const dummy_all_filters = [
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
                        id: 'service_0',
                        type: 'services',
                    },
                    {
                        id: 'service_1',
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
                        id: 'service_1',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'filters',
    },
]

export const getFilterListStub = dummy_all_filters.map(({ id, type }) => ({ id, type }))

export const dummy_all_services = [
    {
        attributes: {
            state: 'Started',
            router: 'readconnroute',
            connections: 0,
            total_connections: 1000001,
            router_diagnostics: {
                queries: 0,
                replayed_transactions: 0,
                ro_transactions: 0,
                route_all: 0,
                route_master: 0,
                route_slave: 0,
                rw_transactions: 0,
                server_query_statistics: [],
            },
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
            listeners: {
                data: [
                    {
                        id: 'RCR-Router-Listener',
                        type: 'listeners',
                    },
                ],
            },
            filters: {
                data: [
                    {
                        id: 'filter_0',
                        type: 'filters',
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
        relationships: {
            listeners: {
                data: [
                    {
                        id: 'RCR-Router-Listener-1',
                        type: 'listeners',
                    },
                    {
                        id: 'RRCR-Router-Listener-2',
                        type: 'listeners',
                    },
                ],
            },
        },
        type: 'services',
    },
]

export const getServiceListStub = dummy_all_services.map(({ id, type }) => ({ id, type }))

export const dummy_service_connection_info = { total_connections: 1000001, connections: 0 }
export const dummy_service_connection_datasets = [
    {
        label: 'Current connections',
        id: 'Current connections',
        type: 'line',
        backgroundColor: 'rgba(171,199,74,0.1)',
        borderColor: 'rgba(171,199,74,1)',
        borderWidth: 1,
        lineTension: 0,
        data: [
            {
                x: 1598516574793,
                y: 0,
            },
        ],
    },
]

export const dummy_all_monitors = [
    {
        attributes: {
            state: 'Running',
            module: 'csmon',
            monitor_diagnostics: {
                master: 'row_server_0',
                master_gtid_domain_id: '0',
                state: 'Running',
                primary: null,
            },
        },
        id: 'monitor_0',
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
        type: 'monitors',
    },
    {
        attributes: { state: 'Stopped', module: 'grmon' },
        id: 'monitor_1',
        links: {},
        relationships: {},
        type: 'monitors',
    },
]

export const getMonitorListStub = dummy_all_monitors.map(({ id, type }) => ({ id, type }))

export const getAllMonitorsMapStub = new Map()
dummy_all_monitors.forEach(ele => {
    getAllMonitorsMapStub.set(ele.id, ele)
})

export const dummy_all_listeners = [
    {
        attributes: {
            parameters: {
                address: '::',
                protocol: 'mariadbclient',
                port: 3308,
            },
            state: 'Running',
        },
        id: 'RCR-Router-Listener',
        relationships: {
            services: {
                data: [
                    {
                        id: 'service_0',
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
        id: 'RCR-Router-Listener-1',
        relationships: {
            services: {
                data: [
                    {
                        id: 'service_1',
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
        id: 'RCR-Router-Listener-2',
        relationships: {
            services: {
                data: [
                    {
                        id: 'service_1',
                        type: 'services',
                    },
                ],
            },
        },
        type: 'listeners',
    },
]

export const serviceStateTableRowsStub = getServiceListStub.map(service => ({
    ...service,
    state: 'Started',
}))

export const dummy_all_sessions = [
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

export const dummy_maxscale_module_parameters = [
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
export const dummy_maxscale_parameters = {
    admin_auth: true,
    log_throttling: {
        count: 0,
        suppress: 0,
        window: 0,
    },
    log_warn_super_user: false,
}

export const dummy_log_data = [
    {
        id: 0,
        message: 'An alert log',
        priority: 'alert',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 1,
        message: 'An error log',
        priority: 'error',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 2,
        message: 'A warning log',
        priority: 'warning',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 3,
        message: 'A notice log',
        priority: 'notice',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 4,
        message: 'An info log',
        priority: 'info',
        timestamp: '2020-09-23 11:04:27',
    },
    {
        id: 5,
        message: 'A debug log',
        priority: 'debug',
        timestamp: '2020-09-23 11:04:27',
    },
]
