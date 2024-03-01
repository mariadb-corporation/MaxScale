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
import {
  LOADING_TIME,
  COMMON_OBJ_OP_TYPES,
  SERVER_OP_TYPES,
  MONITOR_OP_TYPES,
  MXS_OBJ_TYPES,
} from '@/constants'
import { OVERLAY_TRANSPARENT_LOADING } from '@/constants/overlayTypes'

export function useTypy() {
  const vm = getCurrentInstance()
  return vm.appContext.config.globalProperties.$typy
}

export function useLogger() {
  const vm = getCurrentInstance()
  return vm.appContext.config.globalProperties.$logger
}

export function useHelpers() {
  const vm = getCurrentInstance()
  return vm.appContext.config.globalProperties.$helpers
}

export function useHttp() {
  const vm = getCurrentInstance()
  return vm.appContext.config.globalProperties.$http
}

/**
 *
 * @param {string} param.key - default key name
 * @param {boolean} param.isDesc - default isDesc state
 * @returns
 */
export function useSortBy({ key = '', isDesc = false }) {
  const sortBy = ref({ key, isDesc })
  function toggleSortBy(key) {
    if (sortBy.value.isDesc)
      sortBy.value = { key: '', isDesc: false } // non-sort
    else if (sortBy.value.key === key) sortBy.value = { key, isDesc: !sortBy.value.isDesc }
    else sortBy.value = { key, isDesc: false }
  }
  function compareFn(a, b) {
    const aStr = String(a[sortBy.value.key])
    const bStr = String(b[sortBy.value.key])
    return sortBy.value.isDesc ? bStr.localeCompare(aStr) : aStr.localeCompare(bStr)
  }
  return { sortBy, toggleSortBy, compareFn }
}

export function useLoading() {
  const { delay } = useHelpers()
  let isMounting = ref(true)
  const store = useStore()
  const overlay_type = computed(() => store.state.mxsApp.overlay_type)
  const loading = computed(() =>
    isMounting.value || overlay_type.value === OVERLAY_TRANSPARENT_LOADING ? 'primary' : false
  )
  onMounted(async () => await delay(LOADING_TIME).then(() => (isMounting.value = false)))
  return loading
}

export function useGoBack() {
  const store = useStore()
  const router = useRouter()
  const route = useRoute()
  const prev_route = computed(() => store.state.prev_route)
  return () => {
    switch (prev_route.value.name) {
      case 'login':
        router.push('/dashboard/servers')
        break
      case undefined: {
        /**
         * Navigate to parent path. e.g. current path is /dashboard/servers/server_0,
         * it navigates to /dashboard/servers/
         */
        const parentPath = route.path.slice(0, route.path.lastIndexOf('/'))
        if (parentPath) router.push(parentPath)
        else router.push('/dashboard/servers')
        break
      }
      default:
        router.push(prev_route.value.path)
        break
    }
  }
}

/**
 * @param {object} currState - computed property
 * @returns {object}
 */
export function useServerOpMap(currState) {
  const { MAINTAIN, CLEAR, DRAIN, DELETE } = SERVER_OP_TYPES
  const { t } = useI18n()
  const { deleteObj } = useMxsObjActions(MXS_OBJ_TYPES.SERVERS)
  const goBack = useGoBack()
  const store = useStore()
  const currStateMode = computed(() => {
    let currentState = currState.value.toLowerCase()
    if (currentState.indexOf(',') > 0)
      currentState = currentState.slice(0, currentState.indexOf(','))
    return currentState
  })
  return {
    computedMap: computed(() => ({
      [MAINTAIN]: {
        text: t('serverOps.actions.maintain'),
        type: MAINTAIN,
        icon: 'mxs:maintenance',
        iconSize: 22,
        color: 'primary',
        info: t(`serverOps.info.maintain`),
        params: 'set?state=maintenance',
        disabled: currStateMode.value === 'maintenance',
        saveText: 'set',
      },
      [CLEAR]: {
        text: t('serverOps.actions.clear'),
        type: CLEAR,
        icon: 'mxs:restart',
        iconSize: 22,
        color: 'primary',
        info: '',
        params: `clear?state=${currStateMode.value === 'drained' ? 'drain' : currStateMode.value}`,
        disabled: currStateMode.value !== 'maintenance' && currStateMode.value !== 'drained',
      },
      [DRAIN]: {
        text: t('serverOps.actions.drain'),
        type: DRAIN,
        icon: 'mxs:drain',
        iconSize: 22,
        color: 'primary',
        info: t(`serverOps.info.drain`),
        params: `set?state=drain`,
        disabled: currStateMode.value === 'maintenance' || currStateMode.value === 'drained',
      },
      [DELETE]: {
        text: t('serverOps.actions.delete'),
        type: DELETE,
        icon: 'mxs:delete',
        iconSize: 18,
        color: 'error',
        info: '',
        disabled: false,
      },
    })),
    handler: async ({ op, id, forceClosing, callback }) => {
      switch (op.type) {
        case DELETE:
          await deleteObj(id)
          goBack()
          break
        case DRAIN:
        case CLEAR:
        case MAINTAIN:
          await store.dispatch('servers/setOrClearServerState', {
            id,
            opParams: op.params,
            type: op.type,
            callback,
            forceClosing: forceClosing,
          })
          break
      }
    },
  }
}

/**
 * @param {object} currState - computed property
 * @returns {object}
 */
export function useMonitorOpMap(currState) {
  const {
    STOP,
    START,
    DESTROY,
    SWITCHOVER,
    RESET_REP,
    RELEASE_LOCKS,
    FAILOVER,
    REJOIN,
    CS_GET_STATUS,
    CS_STOP_CLUSTER,
    CS_START_CLUSTER,
    CS_SET_READONLY,
    CS_SET_READWRITE,
    CS_ADD_NODE,
    CS_REMOVE_NODE,
  } = MONITOR_OP_TYPES
  const { t } = useI18n()
  return computed(() => ({
    [STOP]: {
      text: t('monitorOps.actions.stop'),
      type: STOP,
      icon: 'mxs:stopped',
      iconSize: 22,
      color: 'primary',
      params: 'stop',
      disabled: currState.value === 'Stopped',
    },
    [START]: {
      text: t('monitorOps.actions.start'),
      type: START,
      icon: 'mxs:running',
      iconSize: 22,
      color: 'primary',
      params: 'start',
      disabled: currState.value === 'Running',
    },
    [DESTROY]: {
      text: t('monitorOps.actions.destroy'),
      type: DESTROY,
      icon: 'mxs:delete',
      iconSize: 18,
      color: 'error',
      disabled: false,
    },
    [SWITCHOVER]: {
      text: t(`monitorOps.actions.${SWITCHOVER}`),
      type: SWITCHOVER,
      icon: 'mxs:switchover',
      iconSize: 24,
      color: 'primary',
      disabled: false,
    },
    [RESET_REP]: {
      text: t(`monitorOps.actions.${RESET_REP}`),
      type: RESET_REP,
      icon: 'mxs:reload',
      iconSize: 20,
      color: 'primary',
      disabled: false,
    },
    [RELEASE_LOCKS]: {
      text: t(`monitorOps.actions.${RELEASE_LOCKS}`),
      type: RELEASE_LOCKS,
      icon: 'mdi-lock-open-outline',
      iconSize: 24,
      color: 'primary',
      disabled: false,
    },
    [FAILOVER]: {
      text: t(`monitorOps.actions.${FAILOVER}`),
      type: FAILOVER,
      icon: 'mxs:failover',
      iconSize: 24,
      disabled: false,
    },
    [REJOIN]: {
      text: t(`monitorOps.actions.${REJOIN}`),
      type: REJOIN,
      //TODO: Add rejoin icon
      disabled: false,
    },
    [CS_GET_STATUS]: { type: CS_GET_STATUS, disabled: false },
    [CS_STOP_CLUSTER]: {
      text: t(`monitorOps.actions.${CS_STOP_CLUSTER}`),
      type: CS_STOP_CLUSTER,
      icon: 'mxs:stopped',
      iconSize: 22,
      color: 'primary',
      disabled: false,
    },
    [CS_START_CLUSTER]: {
      text: t(`monitorOps.actions.${CS_START_CLUSTER}`),
      type: CS_START_CLUSTER,
      icon: 'mxs:running',
      iconSize: 22,
      color: 'primary',
      disabled: false,
    },
    [CS_SET_READONLY]: {
      type: CS_SET_READONLY,
      text: t(`monitorOps.actions.${CS_SET_READONLY}`),
      icon: 'mdi-database-eye-outline',
      iconSize: 24,
      color: 'primary',
      disabled: false,
    },
    [CS_SET_READWRITE]: {
      type: CS_SET_READWRITE,
      text: t(`monitorOps.actions.${CS_SET_READWRITE}`),
      icon: 'mdi-database-edit-outline',
      iconSize: 24,
      color: 'primary',
      disabled: false,
    },
    [CS_ADD_NODE]: {
      type: CS_ADD_NODE,
      text: t(`monitorOps.actions.${CS_ADD_NODE}`),
      icon: 'mdi-plus',
      iconSize: 24,
      color: 'primary',
      disabled: false,
    },
    [CS_REMOVE_NODE]: {
      type: CS_REMOVE_NODE,
      text: t(`monitorOps.actions.${CS_REMOVE_NODE}`),
      icon: 'mxs:delete',
      iconSize: 18,
      color: 'error',
      disabled: false,
    },
  }))
}

export function useCommonObjOpMap(objType) {
  const { DESTROY } = COMMON_OBJ_OP_TYPES
  const { t } = useI18n()
  const goBack = useGoBack()
  const { deleteObj } = useMxsObjActions(objType)
  return {
    map: {
      [DESTROY]: {
        text: `${t('destroy')} ${t(objType, 1)}`,
        type: DESTROY,
        icon: 'mxs:delete',
        iconSize: 18,
        color: 'error',
        info: '',
        disabled: false,
      },
    },
    handler: async ({ op, id }) => {
      if (op.type === COMMON_OBJ_OP_TYPES.DESTROY) await deleteObj(id)
      goBack()
    },
  }
}

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
    patchRelationship: async ({ relationshipType, id, data, callback }) => {
      const [, res] = await tryAsync(
        http.patch(`/${type}/${id}/relationships/${relationshipType}`, { data })
      )
      if (res.status === 204) {
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
export function useObjRelationshipData(type) {
  const typy = useTypy()
  const fetchObjData = useFetchObjData()
  let items = ref([])
  return {
    get: items,
    // relationship data
    setAndFetch: async (data) => {
      items.value = await Promise.all(
        data.map(async ({ id }) => {
          const item = await fetchObjData({ id, type })
          return { id, type, state: typy(item.attributes.state).safeString }
        })
      )
    },
  }
}

/**
 * This function fetch all resources data, if id is not provided,
 * @param {string} [param.id] id of the resource
 * @param {string} param.type type of resource. e.g. servers, services, monitors
 * @param {array} param.fields
 * @return {array|object} Resource data
 */
export function useFetchObjData() {
  const { tryAsync } = useHelpers()
  const http = useHttp()
  const typy = useTypy()
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
