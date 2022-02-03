/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        clusters: {}, // key is the name of the monitor, value is the monitor cluster
        current_cluster: {},
    },
    mutations: {
        SET_CLUSTERS(state, payload) {
            state.clusters = payload
        },
        SET_CURR_CLUSTER(state, payload) {
            state.current_cluster = payload
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
                    if (monitor.attributes.module === 'mariadbmon') {
                        let cluster = getters.getMariadbmonCluster(monitor)
                        if (cluster.children.length) clusters[monitor.id] = cluster
                    }
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
                    cluster = getters.getMariadbmonCluster(monitor)
                commit('SET_CURR_CLUSTER', cluster)
            } catch (e) {
                const logger = this.vue.$logger('store-visualization-fetchClusterById')
                logger.error(e)
            }
        },
    },
    getters: {
        getNodeState: (state, getters, rootState, rootGetters) => {
            return id => {
                const serverData = rootGetters['server/getAllServersMap'].get(id)
                return serverData.attributes.state
            }
        },
        getNodeTitle: (state, getters, rootState, rootGetters) => {
            return id => {
                const serverData = rootGetters['server/getAllServersMap'].get(id)
                if (serverData) {
                    const {
                        attributes: {
                            parameters: { address, port, socket },
                        },
                    } = serverData
                    let title = ''
                    if (socket) title += `${socket}`
                    else title += `${address}:${port}`
                    title += ` (${id})`
                    return title
                }
                return `${id}`
            }
        },
        getCurrConn: (state, getters, rootState, rootGetters) => {
            return id => {
                const serverData = rootGetters['server/getAllServersMap'].get(id)
                if (serverData) {
                    const {
                        attributes: {
                            statistics: { connections },
                        },
                    } = serverData
                    return connections
                }
                return 0
            }
        },
        getMariadbmonCluster: (state, getters) => {
            return monitor => {
                const {
                    id: monitorId,
                    attributes: {
                        monitor_diagnostics: { master: masterName, server_info },
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
                    stroke: '#0e9bc0',
                    children: [], // contains a master server data
                }
                if (masterName)
                    root.children.push({
                        id: masterName,
                        name: masterName,
                        title: getters.getNodeTitle(masterName),
                        state: getters.getNodeState(masterName),
                        connections: getters.getCurrConn(masterName),
                        isMaster: true,
                        stroke: '#0e9bc0',
                        children: [], // contains replicate servers data
                    })
                if (root.children.length)
                    server_info.forEach(server => {
                        const connectionsToMaster = server.slave_connections.filter(
                            conn => conn.master_server_name === masterName
                        )
                        if (connectionsToMaster.length)
                            root.children[0].children.push({
                                id: server.name,
                                name: server.name,
                                title: getters.getNodeTitle(server.name),
                                state: getters.getNodeState(server.name),
                                connections: getters.getCurrConn(server.name),
                                isMaster: false,
                                masterServerName: masterName,
                                server_info: {
                                    ...server,
                                    slave_connections: connectionsToMaster,
                                },
                                stroke: '#0e9bc0',
                            })
                    })
                return root
            }
        },
    },
}
