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
import { http } from '@/utils/axios'
import { t as typy } from 'typy'
import { tryAsync, lodash, uuidv1, capitalizeFirstLetter } from '@/utils/helpers'
import { MXS_OBJ_TYPES } from '@/constants'

const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = MXS_OBJ_TYPES

/**
 * @param {string|object} type - a string literal or a proxy object
 */
export function useMxsObjActions(type) {
  const store = useStore()
  const { t } = useI18n()

  let computedType = computed(() => (typy(type).isString ? type : type.value))

  return {
    fetchObj: async (id) => {
      const [, res] = await tryAsync(http.get(`/${computedType.value}/${id}`))
      if (res.data.data) store.commit(`${computedType.value}/SET_OBJ_DATA`, res.data.data)
    },
    deleteObj: async (id) => {
      const [, res] = await tryAsync(http.delete(`/${computedType.value}/${id}?force=yes`))
      if (res.status === 204) {
        await store.dispatch(`${computedType.value}/fetchAll`)
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [`${capitalizeFirstLetter(t(computedType.value, 1))} ${id} is destroyed`],
          type: 'success',
        })
      }
    },
    /**
     * @param {string} payload.id
     * @param {object} payload.attributes
     * @param {object} [payload.relationships]
     * @param {function} payload.successCb
     */
    createObj: async ({ id, attributes, relationships = {}, successCb }) => {
      const body = {
        data: { id, type: computedType.value, attributes, relationships },
      }
      const [, res] = await tryAsync(http.post(`/${computedType.value}`, body))
      if (res.status === 204) {
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [`${capitalizeFirstLetter(t(computedType.value, 1))} ${id} is created`],
          type: 'success',
        })
        await typy(successCb).safeFunction()
      }
    },
    /**
     * @param {string} payload.id
     * @param {object} payload.data
     * @param {function} payload.callback
     */
    patchParams: async ({ id, data, callback }) => {
      const [, res] = await tryAsync(
        http.patch(`/${computedType.value}/${id}`, {
          data: { id, type: computedType.value, attributes: { parameters: data } },
        })
      )
      if (res.status === 204) {
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [`Parameters of ${id} is updated`],
          type: 'success',
        })
        await typy(callback).safeFunction()
      }
    },
    /**
     * @param {string} payload.id
     * @param {string} payload.relationshipType
     * @param {object} payload.data
     * @param {boolean} [payload.showSnackbar]
     * @param {function} payload.callback
     */
    patchRelationship: async ({ id, relationshipType, data, showSnackbar = true, callback }) => {
      const [, res] = await tryAsync(
        http.patch(`/${computedType.value}/${id}/relationships/${relationshipType}`, { data })
      )
      if (res.status === 204) {
        if (showSnackbar)
          store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
            text: [
              `${capitalizeFirstLetter(t(relationshipType, 2))} ${id} relationships of ${id} is updated`,
            ],
            type: 'success',
          })
        await typy(callback).safeFunction()
      }
    },
    //TODO: Add fetchAll
  }
}

export function useFetchModuleIds() {
  let items = ref([])
  return {
    items,
    fetch: async () => {
      // use an uid to ensure the result includes only ids
      const [, res] = await tryAsync(
        http.get(`/maxscale/modules?load=all&fields[modules]=${uuidv1()}`)
      )
      items.value = typy(res, 'data.data').safeArray.map((item) => item.id)
    },
  }
}

/**
 * Populate data for RelationshipTable
 */
export function useObjRelationshipData() {
  const fetchObjData = useFetchObjData()
  let items = ref([])
  return {
    items,
    /**
     * @param {array} data - relationship data from the API.
     * @param {array} fields - attribute fields
     */
    fetch: async (data, fields = ['state']) => {
      items.value = await Promise.all(
        data.map(async ({ id, type }) => {
          const item = await fetchObjData({ id, type, fields })
          let res = { id, type }
          fields.forEach((field) => {
            if (typy(item.attributes, `${field}`).safeString) res[field] = item.attributes[field]
          })
          return res
        })
      )
    },
  }
}

export function useFetchObjData() {
  /**
   * This function fetch all resources data, if id is not provided,
   * @param {string} [param.id] id of the resource
   * @param {string} param.type type of resource. e.g. servers, services, monitors
   * @param {array} param.fields
   * @param {object} [param.reqConfig] - request config
   * @return {array|object} Resource data
   */
  return async ({ id, type, fields = ['state'], reqConfig = {} }) => {
    let path = `/${type}`
    if (id) path += `/${id}`
    if (fields.length) path += `?fields[${type}]=${fields.join(',')}`
    const [, res] = await tryAsync(http.get(path, reqConfig))
    if (id) return typy(res, 'data.data').safeObjectOrEmpty
    return typy(res, 'data.data').safeArray
  }
}

export function useFetchAllObjIds() {
  const fetch = useFetchObjData()
  let items = ref([])
  return {
    items,
    fetch: async () => {
      const types = Object.values(MXS_OBJ_TYPES)
      const promises = types.map(async (type) => {
        const data = await fetch({ type, fields: ['id'] })
        return data.map((item) => item.id)
      })
      items.value = (await Promise.all(promises)).flat()
    },
  }
}

export function useFetchAllModules() {
  let map = ref({})
  return {
    map,
    fetch: async () => {
      const [, res] = await tryAsync(http.get('/maxscale/modules?load=all'))
      map.value = lodash.groupBy(
        typy(res, 'data.data').safeArray,
        (item) => item.attributes.module_type
      )
    },
    getModules: (type) => {
      switch (type) {
        case SERVICES:
          return typy(map.value['Router']).safeArray
        case SERVERS:
          return typy(map.value['servers']).safeArray
        case MONITORS:
          return typy(map.value['Monitor']).safeArray
        case FILTERS:
          return typy(map.value['Filter']).safeArray
        case LISTENERS: {
          let authenticators = typy(map.value['Authenticator']).safeArray.map((item) => item.id)
          let protocols = lodash.cloneDeep(typy(map.value['Protocol']).safeArray || [])
          if (protocols.length) {
            protocols.forEach((protocol) => {
              protocol.attributes.parameters = protocol.attributes.parameters.filter(
                (o) => o.name !== 'protocol' && o.name !== 'service'
              )
              // Transform authenticator parameter from string type to enum type,
              let authenticatorParamObj = protocol.attributes.parameters.find(
                (o) => o.name === 'authenticator'
              )
              if (authenticatorParamObj) {
                authenticatorParamObj.type = 'enum'
                authenticatorParamObj.enum_values = authenticators
                // add default_value for authenticator
                authenticatorParamObj.default_value = ''
              }
            })
          }
          return protocols
        }
        default:
          return []
      }
    },
  }
}

export function useMxsParams() {
  const store = useStore()
  let parameters = ref({})
  return {
    parameters,
    fetch: async () => {
      const [, res] = await tryAsync(http.get('/maxscale?fields[maxscale]=parameters'))
      parameters.value = typy(res, 'data.data.attributes.parameters').safeObjectOrEmpty
    },
    patch: async ({ data, callback }) => {
      const body = {
        data: {
          id: 'maxscale',
          type: 'maxscale',
          attributes: { parameters: data },
        },
      }
      const [, res] = await tryAsync(http.patch(`/maxscale`, body))
      if (res.status === 204) {
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [`MaxScale parameters is updated`],
          type: 'success',
        })
        await typy(callback).safeFunction()
      }
    },
  }
}

export function useFetchModuleParams() {
  const store = useStore()
  return async (moduleId) => {
    const [, res] = await tryAsync(
      http.get(`/maxscale/modules/${moduleId}?fields[modules]=parameters`)
    )
    const { attributes: { parameters = [] } = {} } = typy(res, 'data.data').safeObjectOrEmpty
    store.commit('SET_MODULE_PARAMETERS', parameters)
  }
}
