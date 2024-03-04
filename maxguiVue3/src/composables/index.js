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
import { LOADING_TIME, COMMON_OBJ_OP_TYPES } from '@/constants'
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

export function useCommonObjOpMap(objType) {
  const { DESTROY } = COMMON_OBJ_OP_TYPES
  const { t } = useI18n()
  const goBack = useGoBack()
  const { deleteObj } = useMxsObjActions(objType)
  return {
    map: {
      [DESTROY]: {
        title: `${t('destroy')} ${t(objType, 1)}`,
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
