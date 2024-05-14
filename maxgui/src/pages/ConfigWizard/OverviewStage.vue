<template>
    <mxs-stage-ctr className="pa-0" bodyClassName="pl-9" footerClassName="pl-9 pt-4">
        <template v-slot:body>
            <div class="fill-height mxs-color-helper text-navigation">
                <p v-for="(item, i) in introductions" :key="i">
                    {{ item }}
                </p>
                <div v-for="step in $mxs_t('configWizardOverview.steps')" :key="step.title">
                    <b>{{ step.title }}</b>
                    <p>{{ step.description }}</p>
                </div>
                <a
                    target="_blank"
                    href="https://mariadb.com/kb/en/mariadb-maxscale-2402-mariadb-maxscale-configuration-guide/"
                    rel="noopener noreferrer"
                    class="rsrc-link"
                >
                    {{ $mxs_t('configWizardOverview.readMoreText') }}
                </a>
            </div>
        </template>
        <template v-slot:footer>
            <v-btn
                small
                height="36"
                color="primary"
                class="font-weight-medium px-7 text-capitalize"
                rounded
                depressed
                @click="$emit('next')"
            >
                {{ $mxs_t('next') }}
            </v-btn>
        </template>
    </mxs-stage-ctr>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'overview-stage',
    computed: {
        introductions() {
            return [
                this.$mxs_t('configWizardOverview.description'),
                this.$mxs_t('configWizardOverview.intro'),
            ]
        },
    },
}
</script>
