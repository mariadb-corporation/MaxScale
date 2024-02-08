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
import ContactCard from '@/components/contact/ContactCard.vue'

const route = useRoute()
const regions = [
  {
    name: 'Europe',
    email: 'sales-EMEA@mariadb.com',
    contacts: [
      { title: 'Denmark', phone: '+45 69 918 495' },
      { title: 'Finland', phone: '+358 94 2597815' },
      { title: 'France & Benelux', phone: '+33 1 82 88 37 38' },
      { title: 'Italy', phone: '+39 02 40042055' },
      { title: 'Sweden', phone: '+46 840 838 825' },
      { title: 'UK & Ireland', phone: '+44 20 3355 2937' },
    ],
  },

  {
    name: 'Americas',
    address: '699 Veterans Blvd, Redwood City, CA 94063, United States',
    email: 'sales-nam@mariadb.com',
    contacts: [{ title: 'Phone', phone: '+1 (855) 562-7423' }],
  },
  {
    name: 'Asia Pacific',
    email: 'sales-APAC@mariadb.com',
  },
]
</script>

<template>
  <ViewWrapper>
    <portal to="view-header__left">
      <h4 style="line-height: normal" class="mb-0 text-navigation text-h4 text-capitalize">
        {{ route.name }}
      </h4>
    </portal>
    <VSheet class="mt-2">
      <VContainer fluid class="mt-8">
        <i18n-t keypath="info.contact" tag="p" class="mb-10" scope="global">
          <template #url>
            <a
              target="_blank"
              rel="noopener noreferrer"
              href="https://mariadb.service-now.com/csm"
              class="rsrc-link"
            >
              {{ $t('supportPortal') }}
            </a>
          </template>
        </i18n-t>
        <VRow>
          <VCol v-for="region in regions" :key="region.name" cols="12" md="6">
            <ContactCard :item="region" />
          </VCol>
        </VRow>
      </VContainer>
    </VSheet>
  </ViewWrapper>
</template>
