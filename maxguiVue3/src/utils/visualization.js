/*
 * Copyright (c) 2023 MariaDB plc
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
import { lodash } from '@/utils//helpers'

/**
 * @param {object} param.server - server object in monitor_diagnostics.server_info
 * @param {object} param.serverData - server attributes
 * @param {string} [param.masterServerName] - master server name
 * @returns {object}
 */
export function genNode({ server, serverData, masterServerName }) {
  let node = {
    id: server.name,
    name: server.name,
    serverData,
    linkColor: '#0e9bc0',
    isMaster: Boolean(!masterServerName),
  }
  if (masterServerName) {
    node.masterServerName = masterServerName
    node.server_info = server
  }
  return node
}

export function genCluster({ monitor, serverMap }) {
  const {
    id: monitorId,
    attributes: { monitor_diagnostics: { master: masterName, server_info } = {}, state, module },
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
      const serverData = serverMap[server.name]
      if (server.slave_connections.length === 0) nodes.push(genNode({ server, serverData }))
      else
        server.slave_connections.forEach((conn) => {
          nodes.push(
            genNode({
              server,
              serverData,
              masterServerName: conn.master_server_name,
            })
          )
        })
      return nodes
    }, [])

    const tree = []
    const nodeMap = lodash.keyBy(nodes, 'id')
    nodes.forEach((node) => {
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
