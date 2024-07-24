/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { LOADING_TIME, COMMON_OBJ_OP_TYPES } from '@/constants'
import { OVERLAY_TRANSPARENT_LOADING } from '@/constants/overlayTypes'
import { http } from '@/utils/axios'
import { delay, getAppEle, uuidv1, getCurrentTimeStamp } from '@/utils/helpers'

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
  return http
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
  const isMounting = ref(true)
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

/**
 * To use this composable, a mousedown event needs to be listened on drag target
 * element and do the followings assign
 * assign true to `isDragging` and event.target to `dragTarget`
 * @returns {DragAndDropData} Reactive object: { isDragging, dragTarget }
 */
export function useDragAndDrop(emitter) {
  const DRAG_TARGET_ID = 'target-drag'
  const isDragging = ref(false)
  const dragTarget = ref(null)

  watch(isDragging, (v) => {
    if (v) addDragEvts()
    else removeDragEvts()
  })
  onBeforeUnmount(() => removeDragEvts())

  /**
   * This copies inherit styles from srcNode to dstNode
   * @param {Object} payload.srcNode - html node to be copied
   * @param {Object} payload.dstNode - target html node to pasted
   */
  function copyNodeStyle({ srcNode, dstNode }) {
    const computedStyle = window.getComputedStyle(srcNode)
    Array.from(computedStyle).forEach((key) =>
      dstNode.style.setProperty(
        key,
        computedStyle.getPropertyValue(key),
        computedStyle.getPropertyPriority(key)
      )
    )
  }

  function removeTargetDragEle() {
    let elem = document.getElementById(DRAG_TARGET_ID)
    if (elem) elem.parentNode.removeChild(elem)
  }

  function addDragTargetEle(e) {
    let cloneNode = dragTarget.value.cloneNode(true)
    cloneNode.setAttribute('id', DRAG_TARGET_ID)
    cloneNode.textContent = dragTarget.value.textContent
    copyNodeStyle({ srcNode: dragTarget.value, dstNode: cloneNode })
    cloneNode.style.position = 'absolute'
    cloneNode.style.top = e.clientY + 'px'
    cloneNode.style.left = e.clientX + 'px'
    cloneNode.style.zIndex = 9999
    getAppEle().appendChild(cloneNode)
  }

  function addDragEvts() {
    document.addEventListener('mousemove', onDragging)
    document.addEventListener('mouseup', onDragEnd)
  }

  function removeDragEvts() {
    document.removeEventListener('mousemove', onDragging)
    document.removeEventListener('mouseup', onDragEnd)
  }

  function onDragging(e) {
    e.preventDefault()
    if (isDragging.value) {
      removeTargetDragEle()
      addDragTargetEle(e)
      emitter('on-dragging', e)
    }
  }

  function onDragEnd(e) {
    e.preventDefault()
    if (isDragging.value) {
      removeTargetDragEle()
      emitter('on-dragend', e)
      isDragging.value = false
    }
  }

  return { isDragging, dragTarget }
}

export function useEventEmitter(KEY) {
  const data = ref(null)
  provide(KEY, data)
  /**
   * @param {string} e - event name
   */
  return (e, payload) => {
    data.value = { id: uuidv1(), event: e, payload }
  }
}

/**
 * @param {object} start - proxy object
 * @param {object} end - proxy object
 */
export function useElapsedTimer(start, end) {
  const count = ref(0)
  const isRunning = computed(() => start.value && !end.value)
  const elapsedTime = computed(() =>
    isRunning.value ? 0 : parseFloat(((end.value - start.value) / 1000).toFixed(4))
  )

  watch(
    isRunning,
    (v) => {
      if (v) updateCount()
      else count.value = 0
    },
    { immediate: true }
  )

  function updateCount() {
    if (!isRunning.value) return
    const now = getCurrentTimeStamp()
    count.value = parseFloat(((now - start.value) / 1000).toFixed(4))
    requestAnimationFrame(updateCount)
  }

  return { isRunning, count, elapsedTime }
}

/**
 * @param {object} param.data - proxy object. Array of objects to process.
 * @param {string} param.field - The field in each object that contains an array of strings or objects.
 * @param {string} [param.subField] - Subfield to extract values from objects within the array specified by `field`.
 * @return {object} proxy object
 */
export function useCountUniqueValues({ data, field, subField }) {
  const total = ref(0)
  watch(
    data,
    (v) => {
      const allItems = v.flatMap((item) => {
        if (Array.isArray(item[field]))
          return subField ? item[field].map((obj) => obj[subField]) : item[field]
        return []
      })
      total.value = Array.from(new Set(allItems)).length
    },
    { immediate: true, deep: true }
  )
  return total
}
