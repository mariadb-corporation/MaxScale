<template>
    <div class="collapsible-ctr">
        <div class="mb-1 d-flex align-center">
            <div class="d-flex align-center" :class="titleWrapperClass">
                <v-btn
                    icon
                    data-test="toggle-btn"
                    class="arrow-toggle"
                    @click="isVisible = !isVisible"
                >
                    <v-icon
                        :class="[isVisible ? 'rotate-down' : 'rotate-right']"
                        size="32"
                        color="navigation"
                    >
                        mdi-chevron-down
                    </v-icon>
                </v-btn>
                <p
                    class="collapsible-ctr-title mb-0 text-body-2 font-weight-bold mxs-color-helper text-navigation text-uppercase"
                >
                    {{ title }}
                    <span
                        v-if="titleInfo || titleInfo === 0"
                        class="ml-1 mxs-color-helper text-grayed-out"
                    >
                        ({{ titleInfo }})
                    </span>
                </p>
                <slot name="title-append" />
            </div>
            <v-spacer />
            <slot name="header-right" />
        </div>
        <v-expand-transition>
            <div v-show="isVisible" data-test="content" class="collapsible-ctr-content">
                <slot />
            </div>
        </v-expand-transition>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    props: {
        titleWrapperClass: { type: [String, Object, Array], default: '' },
        title: { type: String, required: true },
        titleInfo: [String, Number], // option
    },
    data() {
        return {
            isVisible: true,
        }
    },
}
</script>
