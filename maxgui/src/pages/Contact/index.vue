<template>
    <page-wrapper>
        <v-sheet class="mt-2">
            <portal to="page-header">
                <h4
                    style="line-height: normal;"
                    class="mb-0 mxs-color-helper text-navigation text-h4 text-capitalize"
                >
                    {{ $route.name }}
                </h4>
            </portal>
            <v-container fluid class="mt-8">
                <i18n path="mxs.info.contact" tag="p" class="mb-10">
                    <template v-slot:url>
                        <a
                            target="_blank"
                            rel="noopener noreferrer"
                            href="https://mariadb.service-now.com/csm"
                            class="rsrc-link"
                        >
                            {{ $mxs_t('supportPortal') }}
                        </a>
                    </template>
                </i18n>
                <v-row>
                    <v-col v-for="region in regions" :key="region.name" cols="12" md="6">
                        <contact-card :item="region" />
                    </v-col>
                </v-row>
            </v-container>
        </v-sheet>
    </page-wrapper>
</template>

<script>
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
import ContactCard from './ContactCard'

export default {
    name: 'contact',
    components: { ContactCard },
    computed: {
        regions() {
            return [
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
        },
    },
}
</script>

<style lang="scss" scoped>
.contact-tile {
    background-image: linear-gradient(31deg, #f4f2f4 0%, #fff 100%);
}
</style>
