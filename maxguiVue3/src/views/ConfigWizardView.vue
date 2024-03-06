<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import OverviewStage from '@/components/configWizard/OverviewStage.vue'
import ObjStage from '@/components/configWizard/ObjStage.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const { SERVERS, MONITORS } = MXS_OBJ_TYPES

let activeIdxStage = ref(0)
let stageDataMap = ref({})
const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const {
  lodash: { pick },
} = useHelpers()
const fetchObjData = useFetchObjData()

const OVERVIEW_STAGE = { label: t('overview'), component: 'overview-stage' }
const OVERVIEW_STAGE_TYPE = OVERVIEW_STAGE.label
const indexToTypeMap = Object.values(MXS_OBJ_TYPES).reduce(
  (map, type, i) => {
    map[i + 1] = type
    return map
  },
  { 0: OVERVIEW_STAGE_TYPE }
)

const all_modules_map = computed(() => store.state.maxscale.all_modules_map)
const recentlyCreatedObjs = computed(() =>
  Object.values(stageDataMap.value)
    .flatMap((stage) => Object.values(typy(stage, 'newObjMap').safeObjectOrEmpty))
    .reverse()
)

const activeStageType = computed(() => indexToTypeMap[activeIdxStage.value])
watch(
  activeStageType,
  async (v) => {
    if (v !== OVERVIEW_STAGE_TYPE) {
      await fetchExistingObjData(v)
      for (const obj of recentlyCreatedObjs.value) await updateNewObjMap(obj)
    }
  },
  { immediate: true }
)

onBeforeMount(async () => await init())

function initStageMapData() {
  stageDataMap.value = Object.values(MXS_OBJ_TYPES).reduce(
    (map, type) => {
      map[type] = {
        label: t(type, 1),
        newObjMap: {}, // objects that have been recently created using the wizard
        existingObjMap: {}, // existing object data received from API
      }
      return map
    },
    { [OVERVIEW_STAGE_TYPE]: OVERVIEW_STAGE }
  )
}

async function init() {
  initStageMapData()
  if (typy(all_modules_map.value).isEmptyObject) await store.dispatch('maxscale/fetchAllModules')
}

async function fetchExistingObjData(type) {
  const relationshipFields = type === SERVERS ? [MONITORS] : []
  const data = await fetchObjData({ type, fields: ['id', ...relationshipFields] })
  stageDataMap.value[type].existingObjMap = data.reduce((map, item) => {
    map[item.id] = pick(item, ['id', 'type', 'relationships'])
    return map
  }, {})
}

async function updateNewObjMap({ id, type }) {
  const data = await fetchObjData({ id, type, fields: [] })
  stageDataMap.value[type].newObjMap[id] = {
    id,
    type,
    attributes: typy(data, 'attributes').safeObjectOrEmpty,
  }
}
</script>

<template>
  <ViewWrapper :overflow="false" class="fill-height">
    <portal to="view-header__right">
      <GlobalSearch class="ml-4 d-inline-block" />
    </portal>
    <VContainer fluid class="mt-3 fill-height">
      <VRow class="fill-height">
        <VCol cols="9" class="fill-height d-flex flex-row">
          <VTabs v-model="activeIdxStage" direction="vertical" class="v-tabs--vert" hide-slider>
            <VTab v-for="(stage, type, i) in stageDataMap" :key="i">
              <div class="tab-name pa-2 text-navigation font-weight-regular">
                {{ stage.label }}
              </div>
            </VTab>
          </VTabs>
          <VWindow v-model="activeIdxStage" class="w-100 fill-height">
            <VWindowItem v-for="(item, type, i) in stageDataMap" :key="i" class="w-100 fill-height">
              <OverviewStage v-if="activeIdxStage === 0" @next="activeIdxStage++" />
              <ObjStage
                v-else-if="activeIdxStage === i"
                :objType="type"
                :stageDataMap="stageDataMap"
                @next="activeIdxStage++"
                @on-obj-created="updateNewObjMap"
              />
            </VWindowItem>
          </VWindow>
        </VCol>
        <VCol v-if="recentlyCreatedObjs.length" cols="3">
          <div class="d-flex flex-column fill-height pb-10">
            <p class="text-body-2 text-navigation font-weight-bold text-uppercase mb-4">
              {{ $t('recentlyCreatedObjs') }}
            </p>
            <div class="fill-height overflow-y-auto relative">
              <div class="create-objs-ctr absolute pr-2">
                <ConfNode
                  v-for="obj in recentlyCreatedObjs"
                  :key="obj.id"
                  :node="{ id: obj.id, type: obj.type, nodeData: obj }"
                  class="mb-2"
                />
              </div>
            </div>
          </div>
        </VCol>
      </VRow>
    </VContainer>
  </ViewWrapper>
</template>

<style lang="scss" scoped>
.create-objs-ctr {
  width: 100%;
}
</style>
