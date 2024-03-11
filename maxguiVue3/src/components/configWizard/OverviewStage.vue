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
const emit = defineEmits(['next'])
</script>

<template>
  <WizardStageCtr className="pa-0" bodyClassName="pl-9" footerClassName="pl-9">
    <template #body>
      <div class="fill-height text-navigation">
        <p
          v-for="(item, i) in [
            $t('configWizardOverview.description'),
            $t('configWizardOverview.intro'),
          ]"
          :key="i"
          class="mb-4"
        >
          {{ item }}
        </p>
        <div v-for="step in $tm('configWizardOverview.steps')" :key="step.title">
          <b>{{ step.title }}</b>
          <p class="mb-4">{{ step.description }}</p>
        </div>
        <a
          target="_blank"
          href="https://mariadb.com/kb/en/mariadb-maxscale-2402-mariadb-maxscale-configuration-guide/"
          rel="noopener noreferrer"
          class="rsrc-link"
        >
          {{ $t('configWizardOverview.readMoreText') }}
        </a>
      </div>
    </template>
    <template #footer>
      <VBtn
        class="font-weight-medium px-7 text-capitalize"
        color="primary"
        rounded
        variant="flat"
        @click="emit('next')"
      >
        {{ $t('next') }}
      </VBtn>
    </template>
  </WizardStageCtr>
</template>
