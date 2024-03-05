<template>
    <v-card outlined class="contact-tile pa-10">
        <h5 class="font-weight-light mb-4">{{ item.name }}</h5>
        <div v-if="item.address" class="mb-4">
            <h6 :class="contactTitleClass">
                {{ $mxs_t('address') }}
            </h6>
            <span class="mxs-color-helper text-text-navigation">
                {{ item.address }}
            </span>
        </div>
        <div class="mb-4">
            <h6 :class="contactTitleClass">
                {{ $mxs_t('email') }}
            </h6>
            <a :href="`mailto:${item.email}`">{{ item.email }}</a>
        </div>
        <v-row v-if="item.contacts">
            <v-col
                v-for="(contact, index) in item.contacts"
                :key="index"
                cols="12"
                md="6"
                class="pb-0"
            >
                <h6 :class="contactTitleClass">
                    {{ contact.title }}
                </h6>
                <span class="mxs-color-helper text-text-navigation">
                    {{ contact.phone }}
                </span>
            </v-col>
        </v-row>
    </v-card>
</template>

<script>
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

export default {
    name: 'contact-card',
    props: {
        item: { type: Object, required: true },
    },
    computed: {
        contactTitleClass() {
            return 'contact-title text-uppercase mxs-color-helper text-small-text'
        },
    },
}
</script>

<style lang="scss" scoped>
.contact-title {
    font-size: 0.875rem;
    font-weight: 300;
}
</style>
