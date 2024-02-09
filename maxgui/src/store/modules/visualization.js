/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { lodash, genSetMutations } from '@share/utils/helpers'
import { MXS_OBJ_TYPES } from '@share/constants'
import { MRDB_MON } from '@src/constants'
import { QUERY_CONN_BINDING_TYPES } from '@wsSrc/constants'

const states = () => ({
    clusters: {}, // key is the name of the monitor, value is the monitor cluster
    current_cluster: {},
    config_graph_data: [],
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
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
                    if (monitor.attributes.module === MRDB_MON)
                        clusters[monitor.id] = getters.genCluster(monitor)
                })
                commit('SET_CLUSTERS', clusters)
            } catch (e) {
                this.vue.$logger.error(e)
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
                if (monitor.attributes.module === MRDB_MON) cluster = getters.genCluster(monitor)
                commit('SET_CURRENT_CLUSTER', cluster)
            } catch (e) {
                this.vue.$logger.error(e)
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
                this.vue.$logger.error(e)
            }
        },
        /**
         * Check if there is a QueryEditor connected to the provided conn_name and set it as the
         * active worksheet. Otherwise, find an blank worksheet, set it as active show connection
         * dialog with pre-select object
         * @param {String} param.conn_name - connection name
         */
        async chooseQueryEditorWke({ commit }, { type, conn_name }) {
            const { $typy } = this.vue
            const queryEditorConns = QueryConn.getters('queryEditorConns')
            // Find connection
            const queryEditorConn = queryEditorConns.find(
                c => $typy(c, 'meta.name').safeString === conn_name
            )
            /**
             * If it is already bound to a QueryEditor, use the QueryEditor id for
             * setting active worksheet because the QueryEditor id is also Worksheet id.
             */
            if ($typy(queryEditorConn, 'query_editor_id').safeString)
                Worksheet.commit(state => (state.active_wke_id = queryEditorConn.query_editor_id))
            else {
                const blankQueryEditorWke = Worksheet.query()
                    .where(
                        w =>
                            $typy(w, 'etl_task_id').isNull &&
                            $typy(w, 'query_editor_id').isNull &&
                            $typy(w, 'erd_task_id').isNull
                    )
                    .first()
                // Use a blank query editor wke if there is one, otherwise create a new one
                if (blankQueryEditorWke)
                    Worksheet.commit(state => (state.active_wke_id = blankQueryEditorWke.id))
                else Worksheet.dispatch('insertQueryEditorWke')
                commit(
                    'queryConnsMem/SET_PRE_SELECT_CONN_RSRC',
                    { type, id: conn_name },
                    { root: true }
                )
                commit(
                    'mxsWorkspace/SET_CONN_DLG',
                    {
                        is_opened: true,
                        type: QUERY_CONN_BINDING_TYPES.QUERY_EDITOR,
                    },
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
            const { SERVICES, SERVERS, LISTENERS } = MXS_OBJ_TYPES
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
