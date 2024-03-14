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
import { MXS_OBJ_TYPES } from '@/constants'

/**
 * @param {string|object} type - a string literal or a proxy object
 */
export function useMxsObjActions(type) {
  const { tryAsync, capitalizeFirstLetter } = useHelpers()
  const store = useStore()
  const http = useHttp()
  const { t } = useI18n()
  const typy = useTypy()

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
  const { tryAsync, uuidv1 } = useHelpers()
  const http = useHttp()
  const typy = useTypy()
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
  const typy = useTypy()
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
  const { tryAsync } = useHelpers()
  const http = useHttp()
  const typy = useTypy()
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
