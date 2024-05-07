<script setup>
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
  OVERLAY_LOGOUT,
  OVERLAY_LOGGING,
  OVERLAY_TRANSPARENT_LOADING,
} from '@/constants/overlayTypes'

const store = useStore()
const { t } = useI18n()

const overlay_type = computed(() => store.state.mxsApp.overlay_type)
const is_session_alive = computed(() => store.state.mxsApp.is_session_alive)
const transparentLoading = computed(() => overlay_type.value === OVERLAY_TRANSPARENT_LOADING)
const isBaseOverlay = computed(
  () => overlay_type.value === OVERLAY_LOGGING || overlay_type.value === OVERLAY_LOGOUT
)
const isLogging = computed(() => overlay_type.value === OVERLAY_LOGGING)

const msg = computed(() => {
  switch (overlay_type.value) {
    case OVERLAY_LOGGING:
      return t('initializing')
    case OVERLAY_LOGOUT: {
      let txt = t('loggingOut')
      if (!is_session_alive.value) txt = `${t('sessionExpired')}. ${txt}`
      return txt
    }
    default:
      return ''
  }
})
</script>

<template>
  <VOverlay
    :model-value="Boolean(overlay_type)"
    content-class="fill-height w-100 d-flex align-center justify-center"
    z-index="9999"
  >
    <VFadeTransition>
      <VProgressCircular
        v-if="transparentLoading"
        indeterminate
        size="64"
        bg-color="transparent"
        color="background"
      />
      <div
        v-else-if="isBaseOverlay"
        class="overlay-wrapper w-100 fill-height d-flex flex-column justify-center align-center"
      >
        <div class="welcome-txt text-center text-text-subtle">
          <div v-if="isLogging">{{ $t('welcomeTo') }}</div>
          <div class="font-weight-medium">{{ $t('mariaDbMaxScale') }}</div>
        </div>
        <div class="loading-icon">
          <img src="@/assets/icon-globe.svg" alt="MariaDB" />
        </div>
        <div class="text-accent text-center">
          {{ msg }}
        </div>
      </div>
    </VFadeTransition>
  </VOverlay>
</template>

<style lang="scss" scoped>
.overlay-wrapper {
  background: radial-gradient(1100px at 100% 89%, colors.$accent 0%, colors.$deep-ocean 100%);
  .welcome-txt {
    font-size: 30px;
  }
}
</style>
