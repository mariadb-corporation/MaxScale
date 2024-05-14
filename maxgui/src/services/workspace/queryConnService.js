/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ErdTask from '@wsModels/ErdTask'
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import connection from '@/api/sql/connection'
import queries from '@/api/sql/queries'
import store from '@/store'
import worksheetService from '@wsServices/worksheetService'
import prefAndStorageService from '@wsServices/prefAndStorageService'
import etlTaskService from '@wsServices/etlTaskService'
import erdTaskService from '@wsServices/erdTaskService'
import queryTabService from '@wsServices/queryTabService'
import queryEditorService from '@wsServices/queryEditorService'
import {
  tryAsync,
  quotingIdentifier,
  getErrorsArr,
  unquoteIdentifier,
  lodash,
  getConnId,
} from '@/utils/helpers'
import { t as typy } from 'typy'
import { globalI18n as i18n } from '@/plugins/i18n'
import { querySchemaIdentifiers } from '@/store/queryHelper'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import {
  QUERY_CONN_BINDING_TYPES,
  NODE_TYPES,
  NODE_GROUP_TYPES,
  NODE_GROUP_CHILD_TYPES,
  QUERY_LOG_TYPES,
} from '@/constants/workspace'

const { QUERY_EDITOR, QUERY_TAB, ETL_SRC, ETL_DEST, ERD } = QUERY_CONN_BINDING_TYPES

/**
 * @param {string} field - name of the id field
 * @param {string} id
 * @returns {object}
 */
function findConnByField(field, id) {
  return QueryConn.query().where(field, id).first() || {}
}

/**
 * @param {string|function} filter
 * @returns {array}
 */
function findConnsByType(filter) {
  return QueryConn.query().where('binding_type', filter).get()
}

function findAllQueryEditorConns() {
  return findConnsByType(QUERY_EDITOR)
}

function findAllEtlConns() {
  return findConnsByType((v) => v === ETL_SRC || v === ETL_DEST)
}

function findAllErdConns() {
  return findConnsByType(ERD)
}

function findQueryTabConn(id) {
  return findConnByField('query_tab_id', id)
}

function findEtlConns(id) {
  return QueryConn.query().where('etl_task_id', id).get()
}

function findEtlSrcConn(id) {
  return findEtlConns(id).find((c) => c.binding_type === ETL_SRC) || {}
}

function findEtlDestConn(id) {
  return findEtlConns(id).find((c) => c.binding_type === ETL_DEST) || {}
}

/**
 * @param {object} apiConnMap - connections from API mapped by id
 * @param {array} persistentConns - current persistent connections
 * @returns {object} - { alive_conns: [], orphaned_conn_ids: [] }
 * alive_conns: stores connections that exists in the response of a GET to /sql/
 * orphaned_conn_ids: When QueryEditor connection expires but its cloned connections (query tabs)
 * are still alive, those are orphaned connections
 */
function categorizeConns({ apiConnMap, persistentConns }) {
  let alive_conns = [],
    orphaned_conn_ids = []

  persistentConns.forEach((conn) => {
    const connId = conn.id
    if (apiConnMap[connId]) {
      // if this has value, it is a cloned connection from the QueryEditor connection
      const { clone_of_conn_id: queryEditorConnId = '' } = conn || {}
      if (queryEditorConnId && !apiConnMap[queryEditorConnId]) orphaned_conn_ids.push(conn.id)
      else
        alive_conns.push({
          ...conn,
          // update attributes
          attributes: apiConnMap[connId].attributes,
        })
    }
  })

  return { alive_conns, orphaned_conn_ids }
}

/**
 * @param {string} connection_string
 * @returns {string} Database name
 */
function getConnStrDb(connection_string) {
  const matches = connection_string.match(/(database=)\w+/gi) || ['']
  const matched = matches[0]
  return matched.replace(/(database=)+/gi, '')
}

/**
 * If a record is deleted, then the corresponding records in its relational
 * tables (QueryEditor, QueryTab) will have their data refreshed
 * @param {string|function} payload - either a QueryConn id or a callback function that return Boolean (filter)
 */
function cascadeRefreshOnDelete(payload) {
  const entities = QueryConn.filterEntity(QueryConn, payload)
  entities.forEach((entity) => {
    /**
     * refresh its relations, when a connection bound to the QueryEditor is deleted,
     * QueryEditor should be refreshed.
     * If the connection being deleted doesn't have query_editor_id FK but query_tab_id FK,
     * it is a connection bound to QueryTab, thus call queryTabService.cascadeRefresh.
     */
    if (entity.query_editor_id) queryEditorService.cascadeRefresh(entity.query_editor_id)
    else if (entity.query_tab_id)
      queryTabService.cascadeRefresh((t) => t.id === entity.query_tab_id)
    QueryConn.delete(entity.id) // delete itself
  })
}

/**
 * Disconnect a connection and its persisted data
 * @param {String} id - connection id
 */
async function disconnect({ id, showSnackbar }) {
  const config = worksheetService.findConnRequestConfig(id)
  const [e, res] = await tryAsync(connection.delete({ id, config }))
  if (!e && res.status === 204 && showSnackbar)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('success.disconnected')],
      type: 'success',
    })
  cascadeRefreshOnDelete(id)
}

/**
 * @param {Array} connIds - alive connection ids that were cloned from expired QueryEditor connections
 */
async function cleanUpOrphanedConns(connIds) {
  await tryAsync(Promise.all(connIds.map((id) => disconnect({ id }))))
}

/**
 * This handles delete the QueryEditor connection and its query tab connections.
 * @param {Boolean} param.showSnackbar - should show success message or not
 * @param {Number} param.id - connection id that is bound to the QueryEditor
 */
async function cascadeDisconnect({ showSnackbar, id }) {
  const target = QueryConn.find(id)
  if (target) {
    // Delete its clones first
    const clonedConnIds = QueryConn.query()
      .where((c) => c.clone_of_conn_id === id)
      .get()
      .map((c) => c.id)
    await cleanUpOrphanedConns(clonedConnIds)
    await disconnect({ id, showSnackbar })
  }
}

async function disconnectEtlConns(taskId) {
  await tryAsync(Promise.all(findEtlConns(taskId).map(({ id }) => disconnect({ id }))))
}

async function disconnectAll() {
  for (const { id } of findAllQueryEditorConns())
    await cascadeDisconnect({ showSnackbar: false, id })
  await tryAsync(
    Promise.all([...findAllErdConns(), ...findAllEtlConns()].map(({ id }) => disconnect({ id })))
  )
}

/**
 * @param {Boolean} [param.silentValidation] - silent validation (without calling SET_IS_VALIDATING_CONN)
 */
async function validateConns({ silentValidation = false } = {}) {
  if (!silentValidation) store.commit('queryConnsMem/SET_IS_VALIDATING_CONN', true)
  const persistentConns = QueryConn.all()
  let requestConfigs = Worksheet.all().reduce((configs, wke) => {
    const config = worksheetService.findRequestConfig(wke.id)
    const baseUrl = typy(config, 'baseURL').safeString
    if (baseUrl) configs.push(config)
    return configs
  }, [])
  requestConfigs = lodash.uniqBy(requestConfigs, 'baseURL')
  let aliveConns = [],
    orphanedConnIds = []

  for (const config of requestConfigs) {
    const [e, res] = await tryAsync(connection.get(config))
    const apiConnMap = e ? {} : lodash.keyBy(res.data.data, 'id')
    const { alive_conns = [], orphaned_conn_ids = [] } = categorizeConns({
      apiConnMap,
      persistentConns,
    })
    aliveConns = [...aliveConns, ...alive_conns]
    orphanedConnIds = [...orphanedConnIds, ...orphaned_conn_ids]
  }
  QueryConn.update({ data: aliveConns })
  const aliveConnIds = aliveConns.map((c) => c.id)
  //Delete orphaned connections clean-up expired ones
  await cleanUpOrphanedConns(orphanedConnIds)
  cascadeRefreshOnDelete((c) => !aliveConnIds.includes(c.id))
  store.commit('queryConnsMem/SET_IS_VALIDATING_CONN', false)
}

async function fetchAndSetSchemaIdentifiers({ connId, schema }) {
  if (store.state.prefAndStorage.identifier_auto_completion) {
    const { query_tab_id } = QueryConn.find(connId) || {}
    /**
     * use query editor connection instead of query tab connection
     * so it won't block user's query session.
     */
    const queryEditorConnId = QueryConn.getters('activeQueryEditorConn').id
    let identifierCompletionItems = []
    if (schema) {
      const config = Worksheet.getters('activeRequestConfig')
      const schemaName = unquoteIdentifier(schema)
      const results = await querySchemaIdentifiers({
        connId: queryEditorConnId,
        config,
        schemaName,
      })
      const { SCHEMA, TBL } = NODE_TYPES
      const nodeGroupTypes = Object.values(NODE_GROUP_TYPES)
      identifierCompletionItems.push(
        schemaNodeHelper.genCompletionItem({
          name: schemaName,
          type: SCHEMA,
          schemaName,
          parentNameData: { [SCHEMA]: schemaName },
        })
      )
      results.forEach((resultSet, i) => {
        identifierCompletionItems.push(
          ...resultSet.data.map((row) =>
            schemaNodeHelper.genCompletionItem({
              name: row[0],
              type: NODE_GROUP_CHILD_TYPES[nodeGroupTypes[i]],
              parentNameData: { [SCHEMA]: row[1], [TBL]: row[2] },
            })
          )
        )
      })
    }
    QueryTabTmp.update({
      where: query_tab_id,
      data: { schema_identifier_names_completion_items: identifierCompletionItems },
    })
  }
}

/**
 * @param {string} param.connId
 * @param {object} param.config - axios config
 * @param {array} [param.variables] -system variable names defined in prefAndStorage
 */
async function setVariables({
  connId,
  config,
  variables = ['interactive_timeout', 'wait_timeout'],
}) {
  const [e, res] = await tryAsync(
    queries.post({
      id: connId,
      body: {
        sql: variables
          .map((v) => `SET SESSION ${v} = ${store.state.prefAndStorage[v]};`)
          .join('\n'),
      },
      config,
    })
  )
  if (e) store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: getErrorsArr(e), type: 'error' })
  else {
    const errRes = typy(res, 'data.data.attributes.results').safeArray.filter((res) => res.errno)
    if (errRes.length) {
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: errRes.reduce((acc, errObj) => {
          acc += Object.keys(errObj).map((key) => `${key}: ${errObj[key]}`)
          return acc
        }, ''),
        type: 'error',
      })
    }
  }
}

/**
 * @param {string} param.connId - connection id
 * @param {string} param.connName - connection name
 * @param {string} param.schema - quoted schema name
 */
async function useDb({ connId, connName, schema }) {
  const config = Worksheet.getters('activeRequestConfig')
  const now = new Date().valueOf()
  const sql = `USE ${schema};`
  const [e, res] = await tryAsync(queries.post({ id: connId, body: { sql }, config }))
  if (!e && res) {
    let queryName = `Change default database to ${schema}`
    const errObj = typy(res, 'data.data.attributes.results[0]').safeObjectOrEmpty

    if (errObj.errno) {
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: Object.keys(errObj).map((key) => `${key}: ${errObj[key]}`),
        type: 'error',
      })
      queryName = `Failed to change default database to ${schema}`
    } else
      QueryConn.update({ where: connId, data: { active_db: schema } }).then(() => {
        fetchAndSetSchemaIdentifiers({ connId, schema })
      })
    prefAndStorageService.pushQueryLog({
      startTime: now,
      name: queryName,
      sql,
      res,
      connection_name: connName,
      queryType: QUERY_LOG_TYPES.ACTION_LOGS,
    })
  }
}

/**
 * Open a query tab connection
 * @param {object} param
 * @param {object} param.queryEditorConn - QueryEditor connection
 * @param {string} param.query_tab_id - id of the queryTab that binds this connection
 * @param {string} [param.schema] - schema identifier name
 */
async function openQueryTabConn({ queryEditorConn, query_tab_id, schema = '' }) {
  const config = Worksheet.getters('activeRequestConfig')
  const [e, res] = await tryAsync(connection.clone({ id: queryEditorConn.id, config }))
  if (!e && res.status === 201) {
    QueryConn.insert({
      data: {
        id: res.data.data.id,
        attributes: res.data.data.attributes,
        binding_type: QUERY_TAB,
        query_tab_id,
        clone_of_conn_id: queryEditorConn.id,
        meta: queryEditorConn.meta,
      },
    })
    await setVariables({ connId: res.data.data.id, config })
    if (schema)
      await useDb({ connId: res.data.data.id, connName: queryEditorConn.meta.name, schema })
  }
}

/**
 * This clones the QueryEditor connection and bind it to the queryTabs.
 * @param {Array} param.queryTabIds - queryTabIds
 * @param {Object} param.queryEditorConn - connection bound to a QueryEditor
 */
async function cloneQueryEditorConnToQueryTabs({ queryTabIds, queryEditorConn, schema }) {
  await Promise.all(
    queryTabIds.map((id) => openQueryTabConn({ queryEditorConn, query_tab_id: id, schema }))
  )
}

/**
 * @param {Object} param.body - request body
 * @param {Object} param.meta - meta - connection meta
 */
async function openQueryEditorConn({ body, meta }) {
  const config = Worksheet.getters('activeRequestConfig')

  const [e, res] = await tryAsync(connection.open({ body, config }))
  if (e) store.commit('queryConnsMem/SET_CONN_ERR_STATE', true)
  else if (res.status === 201) {
    await setVariables({ connId: res.data.data.id, config })
    const activeQueryEditorConn = QueryConn.getters('activeQueryEditorConn')
    // clean up previous conn after binding the new one
    if (activeQueryEditorConn.id) await cascadeDisconnect({ id: activeQueryEditorConn.id })
    queryEditorService.initEntities()
    const queryEditorId = QueryEditor.getters('activeId')
    const queryEditorConn = {
      id: res.data.data.id,
      attributes: res.data.data.attributes,
      binding_type: QUERY_EDITOR,
      query_editor_id: queryEditorId,
      meta,
    }
    QueryConn.insert({ data: queryEditorConn })

    const activeQueryTabIds = QueryTab.query()
      .where((t) => t.query_editor_id === queryEditorId)
      .get()
      .map((t) => t.id)

    if (activeQueryTabIds.length) {
      const schema = quotingIdentifier(body.db)
      await cloneQueryEditorConnToQueryTabs({
        queryTabIds: activeQueryTabIds,
        queryEditorConn,
        schema,
      })
    }
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('success.connected')],
      type: 'success',
    })
    store.commit('queryConnsMem/SET_CONN_ERR_STATE', false)
  }
}

/**
 * @param {String} param.connection_string - connection_string
 * @param {String} param.binding_type - QUERY_CONN_BINDING_TYPES: Either ETL_SRC or ETL_DEST
 * @param {String} param.etl_task_id - EtlTask ID
 * @param {Object} param.connMeta - connection meta
 * @param {Object} param.taskMeta - etl task meta
 * @param {Boolean} [param.showMsg] - show message related to connection in a snackbar
 */
async function openEtlConn({
  body,
  binding_type,
  etl_task_id,
  connMeta = {},
  taskMeta = {},
  showMsg = false,
}) {
  const config = Worksheet.getters('activeRequestConfig')
  let target
  const [e, res] = await tryAsync(connection.open({ body, config }))
  if (e) store.commit('queryConnsMem/SET_CONN_ERR_STATE', true)
  else if (res.status === 201) {
    let connData = {
      id: res.data.data.id,
      attributes: res.data.data.attributes,
      binding_type,
      meta: connMeta,
      etl_task_id,
    }
    if (binding_type === ETL_DEST) await setVariables({ connId: connData.id, config })
    const { src_type = '', dest_name = '' } = taskMeta
    switch (binding_type) {
      case ETL_SRC:
        target = i18n.t('source').toLowerCase() + `: ${src_type}`
        connData.active_db = quotingIdentifier(getConnStrDb(body.connection_string))
        break
      case ETL_DEST:
        target = i18n.t('destination').toLowerCase() + `: ${dest_name}`
        break
    }
    QueryConn.insert({ data: connData })
    EtlTask.update({
      where: etl_task_id,
      data(obj) {
        obj.meta = { ...obj.meta, ...taskMeta }
      },
    })
  }

  let logMsgs = [i18n.t('success.connectedTo', [target])]

  if (e) logMsgs = [i18n.t('errors.failedToConnectTo', [target]), ...getErrorsArr(e)]

  if (showMsg)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: logMsgs, type: e ? 'error' : 'success' })

  etlTaskService.pushLog({
    id: etl_task_id,
    log: { timestamp: new Date().valueOf(), name: logMsgs.join('\n') },
  })
}

/**
 * @param {Object} param.body - request body
 * @param {Object} param.meta - meta - connection meta
 */
async function openErdConn({ body, meta }) {
  const config = Worksheet.getters('activeRequestConfig')
  const [e, res] = await tryAsync(connection.open({ body, config }))
  if (e) store.commit('queryConnsMem/SET_CONN_ERR_STATE', true)
  else if (res.status === 201) {
    await setVariables({ connId: res.data.data.id, config })
    const activeErdConn = QueryConn.getters('activeErdConn')
    // clean up previous conn after binding the new one
    if (activeErdConn.id) await cascadeDisconnect({ id: activeErdConn.id })
    erdTaskService.initEntities()
    QueryConn.insert({
      data: {
        id: res.data.data.id,
        attributes: res.data.data.attributes,
        binding_type: ERD,
        erd_task_id: ErdTask.getters('activeRecordId'),
        meta,
      },
    })
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('success.connected')],
      type: 'success',
    })
    store.commit('queryConnsMem/SET_CONN_ERR_STATE', false)
  }
}

async function handleOpenConn(params) {
  switch (store.state.workspace.conn_dlg.type) {
    case ERD:
      await openErdConn(params)
      break
    case QUERY_EDITOR:
      await openQueryEditorConn(params)
      break
  }
}

/**
 * @param {Object} param.ids - connections to be reconnected
 * @param {Function} param.onSuccess - on success callback
 * @param {Function} param.onError - on error callback
 */
async function reconnectConns({ ids, onSuccess, onError }) {
  const config = Worksheet.getters('activeRequestConfig')
  const [e, allRes = []] = await tryAsync(
    Promise.all(ids.map((id) => connection.reconnect({ id, config })))
  )
  // call validateConns to get new thread ID
  await validateConns({ silentValidation: true })
  // Set system variables for successfully reconnected connections
  await Promise.all(
    allRes.reduce((acc, res) => {
      if (res.status === 204) acc.push(setVariables({ connId: getConnId(res.config.url), config }))
      return acc
    }, [])
  )
  if (e) {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [...getErrorsArr(e), i18n.t('errors.reconnFailed')],
      type: 'error',
    })
    await typy(onError).safeFunction()
  } else if (allRes.length && allRes.every((promise) => promise.status === 204)) {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('success.reconnected')],
      type: 'success',
    })
    await typy(onSuccess).safeFunction()
  }
}

async function updateActiveDb() {
  const config = Worksheet.getters('activeRequestConfig')
  const { id, active_db } = QueryConn.getters('activeQueryTabConn')
  const [e, res] = await tryAsync(queries.post({ id, body: { sql: 'SELECT DATABASE()' } }, config))
  if (!e && res) {
    let resActiveDb = typy(res, 'data.data.attributes.results[0].data[0][0]').safeString
    resActiveDb = quotingIdentifier(resActiveDb)
    if (!resActiveDb) QueryConn.update({ where: id, data: { active_db: '' } })
    else if (active_db !== resActiveDb)
      QueryConn.update({ where: id, data: { active_db: resActiveDb } })
    fetchAndSetSchemaIdentifiers({ connId: id, schema: resActiveDb })
  }
}

async function enableSqlQuoteShowCreate({ connId, config }) {
  const [, res] = await tryAsync(
    queries.post({ id: connId, body: { sql: 'SET SESSION sql_quote_show_create = 1' }, config })
  )
  const errObj = typy(res, 'data.data.attributes.results[0]').safeObjectOrEmpty
  if (errObj.errno) {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: Object.keys(errObj).map((key) => `${key}: ${errObj[key]}`),
      type: 'error',
    })
  }
}

export default {
  findAllQueryEditorConns,
  findQueryTabConn,
  findEtlConns,
  findEtlSrcConn,
  findEtlDestConn,
  disconnect,
  cascadeDisconnect,
  disconnectEtlConns,
  disconnectAll,
  validateConns,
  openQueryTabConn,
  openEtlConn,
  handleOpenConn,
  reconnectConns,
  updateActiveDb,
  useDb,
  enableSqlQuoteShowCreate,
  setVariables,
}
