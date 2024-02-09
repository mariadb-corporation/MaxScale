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
import ErdTask from '@wsModels/ErdTask'
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import connection from '@/api/sql/connection'
import queries from '@/api/sql/queries'
import queryHelper from '@/store/queryHelper'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import {
  QUERY_CONN_BINDING_TYPES,
  NODE_TYPES,
  NODE_GROUP_TYPES,
  NODE_GROUP_CHILD_TYPES,
  QUERY_LOG_TYPES,
} from '@/constants/workspace'

/**
 *
 * @param {Object} apiConnMap - connections from API mapped by id
 * @param {Array} persistentConns - current persistent connections
 * @returns {Object} - { alive_conns: [], orphaned_conn_ids: [] }
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
 * @param {String} connection_string
 * @returns {String} Database name
 */
function getConnStrDb(connection_string) {
  const matches = connection_string.match(/(database=)\w+/gi) || ['']
  const matched = matches[0]
  return matched.replace(/(database=)+/gi, '')
}

export default {
  namespaced: true,
  actions: {
    /**
     * If a record is deleted, then the corresponding records in its relational
     * tables (QueryEditor, QueryTab) will have their data refreshed
     * @param {String|Function} payload - either a QueryConn id or a callback function that return Boolean (filter)
     */
    cascadeRefreshOnDelete(_, payload) {
      const entities = QueryConn.filterEntity(QueryConn, payload)
      entities.forEach((entity) => {
        /**
         * refresh its relations, when a connection bound to the QueryEditor is deleted,
         * QueryEditor should be refreshed.
         * If the connection being deleted doesn't have query_editor_id FK but query_tab_id FK,
         * it is a connection bound to QueryTab, thus call QueryTab.dispatch('cascadeRefresh').
         */
        if (entity.query_editor_id) QueryEditor.dispatch('cascadeRefresh', entity.query_editor_id)
        else if (entity.query_tab_id)
          QueryTab.dispatch('cascadeRefresh', (t) => t.id === entity.query_tab_id)
        QueryConn.delete(entity.id) // delete itself
      })
    },
    /**
     * This handles delete the QueryEditor connection and its query tab connections.
     * @param {Boolean} param.showSnackbar - should show success message or not
     * @param {Number} param.id - connection id that is bound to the QueryEditor
     */
    async cascadeDisconnect({ dispatch }, { showSnackbar, id }) {
      const target = QueryConn.find(id)
      if (target) {
        // Delete its clones first
        const clonedConnIds = QueryConn.query()
          .where((c) => c.clone_of_conn_id === id)
          .get()
          .map((c) => c.id)
        await dispatch('cleanUpOrphanedConns', clonedConnIds)
        await dispatch('disconnect', { id, showSnackbar })
      }
    },
    /**
     * Disconnect a connection and its persisted data
     * @param {String} id - connection id
     */
    async disconnect({ commit, dispatch }, { id, showSnackbar }) {
      const config = Worksheet.getters('findConnRequestConfig')(id)
      const [e, res] = await this.vue.$helpers.tryAsync(connection.delete({ id, config }))
      if (!e && res.status === 204) {
        if (showSnackbar)
          commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
              text: [this.vue.$t('success.disconnected')],
              type: 'success',
            },
            { root: true }
          )
      }
      dispatch('cascadeRefreshOnDelete', id)
    },
    /**
     * @param {Array} connIds - alive connection ids that were cloned from expired QueryEditor connections
     */
    async cleanUpOrphanedConns({ dispatch }, connIds) {
      await this.vue.$helpers.tryAsync(
        Promise.all(connIds.map((id) => dispatch('disconnect', { id })))
      )
    },
    async disconnectConnsFromTask({ getters }, taskId) {
      await this.vue.$helpers.tryAsync(
        Promise.all(
          getters.findEtlConns(taskId).map(({ id }) => QueryConn.dispatch('disconnect', { id }))
        )
      )
    },
    async disconnectAll({ getters, dispatch }) {
      for (const { id } of getters.queryEditorConns)
        await dispatch('cascadeDisconnect', { showSnackbar: false, id })
      await this.vue.$helpers.tryAsync(
        Promise.all(
          [...getters.erdConns, ...getters.etlConns].map(({ id }) => dispatch('disconnect', { id }))
        )
      )
    },
    /**
     * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
     */
    async validateConns({ commit, dispatch }, { silentValidation = false } = {}) {
      if (!silentValidation) commit('queryConnsMem/SET_IS_VALIDATING_CONN', true, { root: true })
      const persistentConns = QueryConn.all()
      const { $typy, $helpers } = this.vue

      let requestConfigs = Worksheet.all().reduce((configs, wke) => {
        const config = Worksheet.getters('findRequestConfig')(wke.id)
        const baseUrl = $typy(config, 'baseURL').safeString
        if (baseUrl) configs.push(config)
        return configs
      }, [])
      requestConfigs = $helpers.lodash.uniqBy(requestConfigs, 'baseURL')
      let aliveConns = [],
        orphanedConnIds = []

      for (const config of requestConfigs) {
        const [e, res] = await $helpers.tryAsync(connection.get(config))
        const apiConnMap = e ? {} : $helpers.lodash.keyBy(res.data.data, 'id')
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
      await dispatch('cleanUpOrphanedConns', orphanedConnIds)
      dispatch('cascadeRefreshOnDelete', (c) => !aliveConnIds.includes(c.id))
      commit('queryConnsMem/SET_IS_VALIDATING_CONN', false, { root: true })
    },
    /**
     * @param {Object} param.body - request body
     * @param {Object} param.meta - meta - connection meta
     */
    async openQueryEditorConn({ dispatch, commit, getters }, { body, meta }) {
      const config = Worksheet.getters('activeRequestConfig')
      const { $helpers, $t } = this.vue
      const { QUERY_EDITOR } = QUERY_CONN_BINDING_TYPES

      const [e, res] = await $helpers.tryAsync(connection.open({ body, config }))
      if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
      else if (res.status === 201) {
        await dispatch('setVariables', { connId: res.data.data.id, config })
        const activeQueryEditorConn = getters.activeQueryEditorConn
        // clean up previous conn after binding the new one
        if (activeQueryEditorConn.id)
          await QueryConn.dispatch('cascadeDisconnect', {
            id: activeQueryEditorConn.id,
          })
        QueryEditor.dispatch('initQueryEditorEntities')
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
          const schema = $helpers.quotingIdentifier(body.db)
          await dispatch('cloneQueryEditorConnToQueryTabs', {
            queryTabIds: activeQueryTabIds,
            queryEditorConn,
            schema,
          })
        }

        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          {
            text: [$t('success.connected')],
            type: 'success',
          },
          { root: true }
        )
        commit('queryConnsMem/SET_CONN_ERR_STATE', false, { root: true })
      }
    },
    /**
     * This clones the QueryEditor connection and bind it to the queryTabs.
     * @param {Array} param.queryTabIds - queryTabIds
     * @param {Object} param.queryEditorConn - connection bound to a QueryEditor
     */
    async cloneQueryEditorConnToQueryTabs({ dispatch }, { queryTabIds, queryEditorConn, schema }) {
      // clone the connection and bind it to all queryTabs
      await Promise.all(
        queryTabIds.map((id) =>
          dispatch('openQueryTabConn', { queryEditorConn, query_tab_id: id, schema })
        )
      )
    },
    /**
     * Open a query tab connection
     * @param {object} param
     * @param {object} param.queryEditorConn - QueryEditor connection
     * @param {string} param.query_tab_id - id of the queryTab that binds this connection
     * @param {string} [param.schema] - schema identifier name
     */
    async openQueryTabConn({ dispatch }, { queryEditorConn, query_tab_id, schema = '' }) {
      const config = Worksheet.getters('activeRequestConfig')
      const { QUERY_TAB } = QUERY_CONN_BINDING_TYPES

      const [e, res] = await this.vue.$helpers.tryAsync(
        connection.clone({ id: queryEditorConn.id, config })
      )
      if (e) this.vue.$logger.error(e)
      else if (res.status === 201) {
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
        await dispatch('setVariables', { connId: res.data.data.id, config })
        if (schema)
          await dispatch('useDb', {
            connId: res.data.data.id,
            connName: queryEditorConn.meta.name,
            schema,
          })
      }
    },
    /**
     * @param {String} param.connection_string - connection_string
     * @param {String} param.binding_type - QUERY_CONN_BINDING_TYPES: Either ETL_SRC or ETL_DEST
     * @param {String} param.etl_task_id - EtlTask ID
     * @param {Object} param.connMeta - connection meta
     * @param {Object} param.taskMeta - etl task meta
     * @param {Boolean} [param.showMsg] - show message related to connection in a snackbar
     */
    async openEtlConn(
      { commit, dispatch },
      { body, binding_type, etl_task_id, connMeta = {}, taskMeta = {}, showMsg = false }
    ) {
      const config = Worksheet.getters('activeRequestConfig')
      const { $t, $helpers } = this.vue
      const { ETL_SRC, ETL_DEST } = QUERY_CONN_BINDING_TYPES
      let target
      const [e, res] = await $helpers.tryAsync(connection.open({ body, config }))
      if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
      else if (res.status === 201) {
        let connData = {
          id: res.data.data.id,
          attributes: res.data.data.attributes,
          binding_type,
          meta: connMeta,
          etl_task_id,
        }
        if (binding_type === ETL_DEST)
          await dispatch('setVariables', { connId: connData.id, config })
        const { src_type = '', dest_name = '' } = taskMeta
        switch (binding_type) {
          case ETL_SRC:
            target = $t('source').toLowerCase() + `: ${src_type}`
            connData.active_db = $helpers.quotingIdentifier(getConnStrDb(body.connection_string))
            break
          case ETL_DEST:
            target = $t('destination').toLowerCase() + `: ${dest_name}`
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

      let logMsgs = [$t('success.connectedTo', [target])]

      if (e) logMsgs = [$t('errors.failedToConnectTo', [target]), ...$helpers.getErrorsArr(e)]

      if (showMsg)
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          { text: logMsgs, type: e ? 'error' : 'success' },
          { root: true }
        )

      EtlTask.dispatch('pushLog', {
        id: etl_task_id,
        log: { timestamp: new Date().valueOf(), name: logMsgs.join('\n') },
      })
    },
    /**
     * @param {Object} param.body - request body
     * @param {Object} param.meta - meta - connection meta
     */
    async openErdConn({ commit, getters, dispatch }, { body, meta }) {
      const config = Worksheet.getters('activeRequestConfig')
      const { $helpers, $t } = this.vue

      const [e, res] = await $helpers.tryAsync(connection.open({ body, config }))
      if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
      else if (res.status === 201) {
        await dispatch('setVariables', { connId: res.data.data.id, config })
        const activeErdConn = getters.activeErdConn
        // clean up previous conn after binding the new one
        if (activeErdConn.id)
          await QueryConn.dispatch('cascadeDisconnect', { id: activeErdConn.id })
        ErdTask.dispatch('initErdEntities')
        const { ERD } = QUERY_CONN_BINDING_TYPES
        QueryConn.insert({
          data: {
            id: res.data.data.id,
            attributes: res.data.data.attributes,
            binding_type: ERD,
            erd_task_id: ErdTask.getters('activeRecordId'),
            meta,
          },
        })
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          {
            text: [$t('success.connected')],
            type: 'success',
          },
          { root: true }
        )
        commit('queryConnsMem/SET_CONN_ERR_STATE', false, { root: true })
      }
    },
    async handleOpenConn({ rootState, dispatch }, params) {
      const { ERD, QUERY_EDITOR } = QUERY_CONN_BINDING_TYPES
      switch (rootState.mxsWorkspace.conn_dlg.type) {
        case ERD:
          await dispatch('openErdConn', params)
          break
        case QUERY_EDITOR:
          await dispatch('openQueryEditorConn', params)
          break
      }
    },
    /**
     * @param {Object} param.ids - connections to be reconnected
     * @param {Function} param.onSuccess - on success callback
     * @param {Function} param.onError - on error callback
     */
    async reconnectConns({ commit, dispatch }, { ids, onSuccess, onError }) {
      const config = Worksheet.getters('activeRequestConfig')
      const { tryAsync, getConnId, getErrorsArr } = this.vue.$helpers
      const [e, allRes = []] = await tryAsync(
        Promise.all(ids.map((id) => connection.reconnect({ id, config })))
      )
      // call validateConns to get new thread ID
      await dispatch('validateConns', { silentValidation: true })
      // Set system variables for successfully reconnected connections
      await Promise.all(
        allRes.reduce((acc, res) => {
          if (res.status === 204)
            acc.push(
              dispatch('setVariables', {
                connId: getConnId(res.config.url),
                config,
              })
            )
          return acc
        }, [])
      )
      if (e) {
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          {
            text: [...getErrorsArr(e), this.vue.$t('errors.reconnFailed')],
            type: 'error',
          },
          { root: true }
        )
        await this.vue.$typy(onError).safeFunction()
      } else if (allRes.length && allRes.every((promise) => promise.status === 204)) {
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          {
            text: [this.vue.$t('success.reconnected')],
            type: 'success',
          },
          { root: true }
        )
        await this.vue.$typy(onSuccess).safeFunction()
      }
    },
    async updateActiveDb({ getters, dispatch }) {
      const config = Worksheet.getters('activeRequestConfig')
      const { id, active_db } = getters.activeQueryTabConn
      const [e, res] = await this.vue.$helpers.tryAsync(
        queries.post({ id, body: { sql: 'SELECT DATABASE()' } }, config)
      )
      if (!e && res) {
        let resActiveDb = this.vue.$typy(
          res,
          'data.data.attributes.results[0].data[0][0]'
        ).safeString
        resActiveDb = this.vue.$helpers.quotingIdentifier(resActiveDb)
        if (!resActiveDb) QueryConn.update({ where: id, data: { active_db: '' } })
        else if (active_db !== resActiveDb)
          QueryConn.update({ where: id, data: { active_db: resActiveDb } })
        dispatch('fetchAndSetSchemaIdentifiers', { connId: id, schema: resActiveDb })
      }
    },
    /**
     * @param {string} param.connId - connection id
     * @param {string} param.connName - connection name
     * @param {string} param.schema - quoted schema name
     */
    async useDb({ commit, dispatch }, { connId, connName, schema }) {
      const config = Worksheet.getters('activeRequestConfig')
      const now = new Date().valueOf()
      const sql = `USE ${schema};`
      const [e, res] = await this.vue.$helpers.tryAsync(
        queries.post({ id: connId, body: { sql }, config })
      )
      if (!e && res) {
        let queryName = `Change default database to ${schema}`
        const errObj = this.vue.$typy(res, 'data.data.attributes.results[0]').safeObjectOrEmpty

        if (errObj.errno) {
          commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
              text: Object.keys(errObj).map((key) => `${key}: ${errObj[key]}`),
              type: 'error',
            },
            { root: true }
          )
          queryName = `Failed to change default database to ${schema}`
        } else
          QueryConn.update({ where: connId, data: { active_db: schema } }).then(() =>
            dispatch('fetchAndSetSchemaIdentifiers', { connId, schema })
          )
        dispatch(
          'prefAndStorage/pushQueryLog',
          {
            startTime: now,
            name: queryName,
            sql,
            res,
            connection_name: connName,
            queryType: QUERY_LOG_TYPES.ACTION_LOGS,
          },
          { root: true }
        )
      }
    },
    async enableSqlQuoteShowCreate({ commit }, { connId, config }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        queries.post({
          id: connId,
          body: { sql: 'SET SESSION sql_quote_show_create = 1' },
          config,
        })
      )
      const errObj = this.vue.$typy(res, 'data.data.attributes.results[0]').safeObjectOrEmpty
      if (errObj.errno) {
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          {
            text: Object.keys(errObj).map((key) => `${key}: ${errObj[key]}`),
            type: 'error',
          },
          { root: true }
        )
      }
    },
    async fetchAndSetSchemaIdentifiers({ getters, rootState }, { connId, schema }) {
      if (rootState.prefAndStorage.identifier_auto_completion) {
        const { query_tab_id } = QueryConn.find(connId) || {}
        /**
         * use query editor connection instead of query tab connection
         * so it won't block user's query session.
         */
        const queryEditorConnId = getters.activeQueryEditorConn.id
        let identifierCompletionItems = []
        if (schema) {
          const config = Worksheet.getters('activeRequestConfig')
          const schemaName = this.vue.$helpers.unquoteIdentifier(schema)
          const results = await queryHelper.fetchSchemaIdentifiers({
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
                  parentNameData: {
                    [SCHEMA]: row[1],
                    [TBL]: row[2],
                  },
                })
              )
            )
          })
        }
        QueryTabTmp.update({
          where: query_tab_id,
          data: {
            schema_identifier_names_completion_items: identifierCompletionItems,
          },
        })
      }
    },
    /**
     *
     * @param {string} param.connId
     * @param {object} param.config - axios config
     * @param {array} [param.variables] -system variable names defined in prefAndStorage
     */
    async setVariables(
      { commit, rootState },
      { connId, config, variables = ['interactive_timeout', 'wait_timeout'] }
    ) {
      const [e, res] = await this.vue.$helpers.tryAsync(
        queries.post({
          id: connId,
          body: {
            sql: variables
              .map((v) => `SET SESSION ${v} = ${rootState.prefAndStorage[v]};`)
              .join('\n'),
          },
          config,
        })
      )
      if (e)
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          { text: this.vue.$helpers.getErrorsArr(e), type: 'error' },
          { root: true }
        )
      else {
        const errRes = this.vue
          .$typy(res, 'data.data.attributes.results')
          .safeArray.filter((res) => res.errno)
        if (errRes.length) {
          commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
              text: errRes.reduce((acc, errObj) => {
                acc += Object.keys(errObj).map((key) => `${key}: ${errObj[key]}`)
                return acc
              }, ''),
              type: 'error',
            },
            { root: true }
          )
        }
      }
    },
  },
  getters: {
    activeQueryEditorConn: () =>
      QueryConn.query().where('query_editor_id', QueryEditor.getters('activeId')).first() || {},
    activeQueryTabConn: () => {
      const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
      if (!activeQueryTabId) return {}
      return QueryConn.query().where('query_tab_id', activeQueryTabId).first() || {}
    },
    activeSchema: (state, getters) => getters.activeQueryTabConn.active_db || '',
    activeEtlConns: () =>
      QueryConn.query().where('etl_task_id', Worksheet.getters('activeRecord').etl_task_id).get(),
    activeEtlSrcConn: (state, getters) =>
      getters.activeEtlConns.find((c) => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_SRC) || {},
    activeErdConn: () =>
      QueryConn.query().where('erd_task_id', ErdTask.getters('activeRecordId')).first() || {},
    queryEditorConns: () =>
      QueryConn.query().where('binding_type', QUERY_CONN_BINDING_TYPES.QUERY_EDITOR).get(),
    etlConns: () => {
      const { ETL_SRC, ETL_DEST } = QUERY_CONN_BINDING_TYPES
      return QueryConn.query()
        .where('binding_type', (v) => v === ETL_SRC || v === ETL_DEST)
        .get()
    },
    erdConns: () => {
      const { ERD } = QUERY_CONN_BINDING_TYPES
      return QueryConn.query()
        .where('binding_type', (v) => v === ERD)
        .get()
    },
    // Method-style getters (Uncached getters)
    findQueryTabConn: () => (query_tab_id) =>
      QueryConn.query().where('query_tab_id', query_tab_id).first() || {},
    findEtlConns: () => (etl_task_id) => QueryConn.query().where('etl_task_id', etl_task_id).get(),
    findEtlSrcConn: (state, getters) => (etl_task_id) =>
      getters
        .findEtlConns(etl_task_id)
        .find((c) => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_SRC) || {},
    findEtlDestConn: (state, getters) => (etl_task_id) =>
      getters
        .findEtlConns(etl_task_id)
        .find((c) => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_DEST) || {},
  },
}
