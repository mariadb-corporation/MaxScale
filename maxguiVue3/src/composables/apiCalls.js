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

export function useMxsObjActions(type) {
  const { tryAsync, capitalizeFirstLetter } = useHelpers()
  const store = useStore()
  const http = useHttp()
  const { t } = useI18n()
  const typy = useTypy()
  return {
    deleteObj: async (id) => {
      const [, res] = await tryAsync(http.delete(`/${type}/${id}?force=yes`))
      if (res.status === 204) {
        await store.dispatch(`${type}/fetchAll`)
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [`${capitalizeFirstLetter(t(type, 1))} ${id} is destroyed`],
          type: 'success',
        })
      }
    },
    fetchObj: async (id) => {
      const [, res] = await tryAsync(http.get(`/${type}/${id}`))
      if (res.data.data) store.commit(`${type}/SET_OBJ_DATA`, res.data.data)
    },
    patchParams: async ({ id, data, callback }) => {
      const [, res] = await tryAsync(
        http.patch(`/${type}/${id}`, {
          data: { id, type, attributes: { parameters: data } },
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
    patchRelationship: async ({ id, relationshipType, data, showSnackbar = true, callback }) => {
      const [, res] = await tryAsync(
        http.patch(`/${type}/${id}/relationships/${relationshipType}`, { data })
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
    //TODO: Add fetchAll, createObj, ...
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
   * @return {array|object} Resource data
   */
  return async ({ id, type, fields = ['state'] }) => {
    let path = `/${type}`
    if (id) path += `/${id}`
    path += `?fields[${type}]=${fields.join(',')}`
    const [, res] = await tryAsync(http.get(path))
    if (id) return typy(res, 'data.data').safeObjectOrEmpty
    return typy(res, 'data.data').safeArray
  }
}

export function useFetchAllObjIds() {
  const fetch = useFetchObjData()
  return async () => {
    const types = Object.values(MXS_OBJ_TYPES)
    const promises = types.map(async (type) => {
      const data = await fetch({ type, fields: ['id'] })
      return data.map((item) => item.id)
    })
    return await Promise.all(promises)
  }
}
