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
defineProps({
  item: { type: Object, required: true },
})

const titleClass = 'contact-title text-uppercase text-small-text text-h6'
</script>

<template>
  <VCard flat class="contact-tile pa-10" border>
    <h5 class="font-weight-light mb-4 text-h5">{{ item.name }}</h5>
    <div v-if="item.address" class="mb-4">
      <h6 :class="titleClass">
        {{ $t('address') }}
      </h6>
      <span class="text-navigation">
        {{ item.address }}
      </span>
    </div>
    <div class="mb-4">
      <h6 :class="titleClass">
        {{ $t('email') }}
      </h6>
      <a :href="`mailto:${item.email}`">{{ item.email }}</a>
    </div>
    <VRow v-if="item.contacts">
      <VCol v-for="(contact, index) in item.contacts" :key="index" cols="12" md="6" class="pb-0">
        <h6 :class="titleClass">
          {{ contact.title }}
        </h6>
        <span class="text-navigation">
          {{ contact.phone }}
        </span>
      </VCol>
    </VRow>
  </VCard>
</template>

<style lang="scss" scoped>
.contact-tile {
  background-image: linear-gradient(31deg, #f4f2f4 0%, #fff 100%);
}
.contact-title {
  font-size: 0.875rem !important;
  font-weight: 300 !important;
}
</style>
