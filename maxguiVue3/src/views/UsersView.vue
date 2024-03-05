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
import UserForm from '@/components/users/UserForm.vue'
import { USER_ADMIN_ACTIONS, USER_ROLES } from '@/constants'

const { DELETE, UPDATE, ADD } = USER_ADMIN_ACTIONS
const store = useStore()
const { dateFormat } = useHelpers()
const typy = useTypy()
const { map: opMap, handler: opHandler } = useUserOpMap()
const loading = useLoading()

const search_keyword = computed(() => store.state.search_keyword)
const all_inet_users = computed(() => store.state.users.all_inet_users)
const logged_in_user = computed(() => store.state.users.logged_in_user)

const commonHeaderProps = { cellProps: { class: 'pl-3 pr-0' }, headerProps: { class: 'pl-3 pr-0' } }
const headers = [
  {
    title: 'Username',
    value: 'id',
    sortable: true,
    autoTruncate: true,
    cellProps: { style: { maxWidth: '250px' } },
  },
  { title: 'Role', value: 'role', sortable: true, ...commonHeaderProps },
  { title: 'Type', value: 'type', sortable: true, ...commonHeaderProps },
  { title: 'Created', value: 'created', sortable: true, ...commonHeaderProps },
  { title: 'Last Updated', value: 'last_update', sortable: true, ...commonHeaderProps },
  { title: 'Last Login', value: 'last_login', sortable: true, ...commonHeaderProps },
  {
    title: '',
    value: 'action',
    cellProps: { class: 'pl-0 pr-3', align: 'end' },
    headerProps: { class: 'pl-0 pr-3', align: 'end' },
  },
]
let tableHeight = ref(0)

let form = ref(getDefUserObj())
let actionType = ref(ADD)
let isDlgOpened = ref(false)
const tableWrapper = ref(null)

const isAdmin = computed(() => store.getters['users/isAdmin'])

const items = computed(() =>
  all_inet_users.value.map((user) => {
    const { id, type, attributes: { account, created, last_update, last_login } = {} } = user
    return {
      id,
      role: account,
      type,
      created: created ? dateFormat({ value: created }) : '',
      last_update: last_update ? dateFormat({ value: last_update }) : '',
      last_login: last_login ? dateFormat({ value: last_login }) : '',
    }
  })
)
watch(isDlgOpened, (v) => {
  if (!v) form.value = getDefUserObj()
})

onMounted(async () => {
  setTableHeight()
  await fetchAll()
})

async function fetchAll() {
  await store.dispatch('users/fetchAllNetworkUsers')
}

function isLoggedInUser(item) {
  return typy(logged_in_user.value, 'name').safeString === item.id
}

function setTableHeight() {
  nextTick(() => {
    const height = typy(tableWrapper.value, 'clientHeight').safeNumber
    if (height) tableHeight.value = height - 16
  })
}

function getDefUserObj() {
  return { id: '', password: '', role: USER_ROLES.ADMIN }
}

/**
 * @param {String} param.type - delete||update||add
 * @param {Object} param.user - user object
 */
function actionHandler({ type, user }) {
  isDlgOpened.value = true
  actionType.value = type
  form.value = user ? { id: user.id, password: '', role: user.role } : getDefUserObj()
}

async function confirmSave() {
  await opHandler({
    type: actionType.value,
    payload: form.value,
    callback: fetchAll,
  })
}
</script>

<template>
  <ViewWrapper :overflow="false">
    <portal to="view-header__right">
      <GlobalSearch />
      <VBtn
        v-if="isAdmin"
        :width="160"
        :height="36"
        rounded
        variant="outlined"
        class="ml-4 text-capitalize px-8 font-weight-medium"
        size="small"
        color="primary"
        @click="actionHandler({ type: ADD })"
      >
        + {{ opMap[ADD].title }}
      </VBtn>
    </portal>

    <VSheet class="d-flex flex-column fill-height mt-12">
      <div ref="tableWrapper" class="fill-height">
        <VDataTable
          :headers="headers"
          :items="items"
          :search="search_keyword"
          :height="tableHeight"
          fixed-header
          :itemsPerPage="-1"
          :sort-by="[{ key: 'id', order: 'asc' }]"
          :loading="loading"
          class="users-table"
        >
          <template #item="{ item, columns }">
            <VHover>
              <template #default="{ isHovering, props }">
                <tr class="v-data-table__tr" v-bind="props">
                  <CustomTblCol
                    v-for="(h, i) in columns"
                    :key="h.value"
                    :value="item[h.value]"
                    :name="h.value"
                    :search="search_keyword"
                    :autoTruncate="h.autoTruncate"
                    :class="{ 'font-weight-bold': isLoggedInUser(item) }"
                    v-bind="columns[i].cellProps"
                  >
                    <template v-if="isAdmin" #[`item.action`]>
                      <TooltipBtn
                        v-for="op in [
                          opMap[UPDATE],
                          ...(isLoggedInUser(item) ? [] : [opMap[DELETE]]),
                        ]"
                        :key="op.title"
                        density="comfortable"
                        icon
                        variant="text"
                        size="small"
                        :color="op.color"
                        @click="actionHandler({ type: op.type, user: item })"
                      >
                        <template v-slot:btn-content>
                          <VIcon v-show="isHovering" size="18" :icon="op.icon" />
                        </template>
                        {{ op.title }}
                      </TooltipBtn>
                    </template>
                  </CustomTblCol>
                </tr>
              </template>
            </VHover>
          </template>
          <template #bottom />
        </VDataTable>
      </div>
      <BaseDlg
        v-model="isDlgOpened"
        :saveText="actionType"
        :title="$t(`userOps.actions.${actionType}`)"
        :onSave="confirmSave"
      >
        <template #body>
          <i18n-t
            v-if="actionType === DELETE"
            data-test="confirmations-text"
            :keypath="`confirmations.${DELETE}`"
            tag="p"
            scope="global"
          >
            <template #default>
              <b>{{ form.id }}</b>
            </template>
          </i18n-t>

          <div v-if="actionType === UPDATE" class="d-flex align-center mb-2">
            <VIcon size="20" class="mr-1" icon="mxs:user" />
            <span>{{ form.id }}</span>
          </div>
        </template>
        <template v-if="actionType === UPDATE || actionType === ADD" #form-body>
          <UserForm v-model="form" :type="actionType" />
        </template>
      </BaseDlg>
    </VSheet>
  </ViewWrapper>
</template>
