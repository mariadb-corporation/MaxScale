/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import { lodash } from '@share/utils/helpers'

export default {
    namespaced: true,
    state: {
        clusters: {}, // key is the name of the monitor, value is the monitor cluster
        current_cluster: {},
        config_graph_data: [],
    },
    mutations: {
        SET_CLUSTERS(state, payload) {
            state.clusters = payload
        },
        SET_CURR_CLUSTER(state, payload) {
            state.current_cluster = payload
        },
        SET_CONFIG_GRAPH_DATA(state, payload) {
            state.config_graph_data = payload
        },
    },
    actions: {
        async discoveryClusters({ commit, dispatch, rootState, getters }) {
            try {
                await Promise.all([
                    dispatch('server/fetchAllServers', {}, { root: true }),
                    dispatch('monitor/fetchAllMonitors', {}, { root: true }),
                ])
                let clusters = {}
                rootState.monitor.all_monitors.forEach(monitor => {
                    //TODO: Handle other monitors, now it only handles mariadbmon
                    if (monitor.attributes.module === 'mariadbmon')
                        clusters[monitor.id] = getters.genCluster(monitor)
                })
                commit('SET_CLUSTERS', clusters)
            } catch (e) {
                const logger = this.vue.$logger('store-visualization-discoveryClusters')
                logger.error(e)
            }
        },
        async fetchClusterById({ commit, dispatch, rootState, getters }, id) {
            try {
                await Promise.all([
                    dispatch('server/fetchAllServers', {}, { root: true }),
                    dispatch('monitor/fetchMonitorById', id, { root: true }),
                ])
                let cluster = {}
                const monitor = rootState.monitor.current_monitor
                //TODO: Handle other monitors, now it only handles mariadbmon
                if (monitor.attributes.module === 'mariadbmon')
                    cluster = getters.genCluster(monitor)
                commit('SET_CURR_CLUSTER', cluster)
            } catch (e) {
                const logger = this.vue.$logger('store-visualization-fetchClusterById')
                logger.error(e)
            }
        },
        async fetchConfigData({ commit, dispatch, getters }) {
            try {
                await Promise.all([
                    dispatch('monitor/fetchAllMonitors', {}, { root: true }),
                    dispatch('server/fetchAllServers', {}, { root: true }),
                    dispatch('service/fetchAllServices', {}, { root: true }),
                    dispatch('filter/fetchAllFilters', {}, { root: true }),
                    dispatch('listener/fetchAllListeners', {}, { root: true }),
                ])
                commit('SET_CONFIG_GRAPH_DATA', getters.getConfigGraphData)
            } catch (e) {
                const logger = this.vue.$logger('store-visualization-fetchConfigData')
                logger.error(e)
            }
        },
        /**
         * Check if there is a worksheet connected to the provided conn_name make it the current
         * active worksheet if it's not. Otherwise, find an empty worksheet(has not been bound to a connection),
         * set it as active and dispatch SET_PRE_SELECT_CONN_RSRC to open connection dialog
         * @param {String} param.conn_name - connection name
         */
        async chooseActiveQueryEditorWke(
            { commit, dispatch, rootState, rootGetters },
            { type, conn_name }
        ) {
            const conn = rootGetters['queryConn/getWkeConns'].find(c => c.name === conn_name)
            const targetWke = rootState.wke.worksheets_arr.find(
                w => w.id === this.vue.$typy(conn, 'wke_id_fk').safeString
            )
            if (targetWke) {
                if (rootState.wke.active_wke_id !== targetWke.id)
                    commit('wke/SET_ACTIVE_WKE_ID', targetWke.id, { root: true })
                commit(
                    'querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP',
                    {
                        id: targetWke.id,
                        payload: rootState.querySession.active_session_by_wke_id_map[targetWke.id],
                    },
                    { root: true }
                )
            }
            // Use a blank wke if there is one, otherwise create a new one
            else {
                const blankSession = rootState.querySession.query_sessions.find(
                    s => this.vue.$typy(s, 'active_sql_conn').isEmptyObject
                )
                const blankWke = rootState.wke.worksheets_arr.find(
                    wke => wke.id === this.vue.$typy(blankSession, 'wke_id_fk').safeString
                )
                if (blankWke) {
                    commit('wke/SET_ACTIVE_WKE_ID', blankWke.id, { root: true })
                    commit(
                        'querySession/SET_ACTIVE_SESSION_BY_WKE_ID_MAP',
                        { id: blankWke.id, payload: blankSession.id },
                        { root: true }
                    )
                } else await dispatch('wke/addNewWs', { root: true })
                commit(
                    'queryConn/SET_PRE_SELECT_CONN_RSRC',
                    { type, id: conn_name },
                    { root: true }
                )
            }
        },
    },
    getters: {
        getServerData: (state, getters, rootState, rootGetters) => {
            return id => {
                return rootGetters['server/getAllServersMap'].get(id)
            }
        },
        genNode: (state, getters) => {
            /**
             *
             * @param {object} param.server - server object in monitor_diagnostics.server_info
             * @param {String} param.masterServerName - master server name
             * @returns {object}
             */
            return ({ server, masterServerName }) => {
                let node = {
                    id: server.name,
                    name: server.name,
                    serverData: getters.getServerData(server.name),
                    linkColor: '#0e9bc0',
                    isMaster: Boolean(!masterServerName),
                }
                if (masterServerName) {
                    node.masterServerName = masterServerName
                    node.server_info = server
                }

                return node
            }
        },
        genCluster: (state, getters) => {
            return monitor => {
                const {
                    id: monitorId,
                    attributes: {
                        monitor_diagnostics: { master: masterName, server_info } = {},
                        state,
                        module,
                    },
                } = monitor
                // root node contain monitor data
                let root = {
                    id: monitorId,
                    name: monitorId,
                    state,
                    module,
                    linkColor: '#0e9bc0',
                    children: [], // contains a master server data
                    monitorData: monitor.attributes,
                }
                if (masterName) {
                    const nodes = server_info.reduce((nodes, server) => {
                        if (server.slave_connections.length === 0)
                            nodes.push(getters.genNode({ server }))
                        else
                            server.slave_connections.forEach(conn => {
                                nodes.push(
                                    getters.genNode({
                                        server,
                                        masterServerName: conn.master_server_name,
                                    })
                                )
                            })
                        return nodes
                    }, [])

                    const tree = []
                    const nodeMap = lodash.keyBy(nodes, 'id')
                    nodes.forEach(node => {
                        if (node.masterServerName) {
                            const parent = nodeMap[node.masterServerName]
                            if (parent) {
                                // Add the current node as a child of its parent.
                                if (!parent.children) parent.children = []
                                parent.children.push(node)
                            }
                        } else tree.push(node)
                    })
                    root.children = tree
                }

                return root
            }
        },
        /*
         * This generates data for d3-dag StratifyOperator
         */
        getConfigGraphData: (state, getters, rootState) => {
            const {
                service: { all_services },
                server: { all_servers },
                monitor: { all_monitors },
                listener: { all_listeners },
            } = rootState
            let data = []
            const { SERVICES, SERVERS, LISTENERS } = rootState.app_config.RELATIONSHIP_TYPES
            const rsrcData = [all_services, all_servers, all_listeners, all_monitors]
            rsrcData.forEach(rsrc =>
                rsrc.forEach(item => {
                    const { id, type, relationships } = item
                    let node = { id, type, nodeData: item, parentIds: [] }
                    /**
                     * DAG graph requires root nodes.
                     * With current data from API, accurate links between nodes can only be found by
                     * checking the relationships data of a service. So monitors are root nodes here.
                     * This adds parent node ids for services, servers and listeners node to create links except
                     * monitors, as the links between monitors and servers or monitors and services are created
                     * already. This is an intention to prevent circular reference.
                     */
                    let relationshipTypes = []
                    switch (type) {
                        case SERVICES:
                            // a service can also target services or monitors
                            relationshipTypes = ['servers', 'services', 'monitors']
                            break
                        case SERVERS:
                            relationshipTypes = ['monitors']
                            break
                        case LISTENERS:
                            relationshipTypes = ['services']
                            break
                    }
                    Object.keys(relationships).forEach(key => {
                        if (relationshipTypes.includes(key))
                            relationships[key].data.forEach(n => {
                                node.parentIds.push(n.id) // create links
                            })
                    })
                    data.push(node)
                })
            )
            return data
        },
    },
}
