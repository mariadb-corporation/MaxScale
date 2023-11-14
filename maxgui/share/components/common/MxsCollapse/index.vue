<template>
    <div
        :class="wrapperClass"
        v-on="
            $typy(onEdit).isFunction && editable
                ? {
                      mouseenter: e => (showEditBtn = true),
                      mouseleave: e => (showEditBtn = false),
                  }
                : null
        "
    >
        <div class="mb-1 d-flex align-center">
            <div class="d-flex align-center" :class="titleWrapperClass">
                <slot
                    name="arrow-toggle"
                    :toggleOnClick="toggleOnClick"
                    :isContentVisible="isContentVisible"
                >
                    <v-btn icon class="arrow-toggle" @click="toggleOnClick">
                        <v-icon
                            :class="[isContentVisible ? 'rotate-down' : 'rotate-right']"
                            size="32"
                            color="navigation"
                        >
                            mdi-chevron-down
                        </v-icon>
                    </v-btn>
                </slot>
                <p
                    class="mxs-collapse-title mb-0 text-body-2 font-weight-bold mxs-color-helper text-navigation text-uppercase"
                >
                    {{ title }}
                    <span
                        v-if="titleInfo || titleInfo === 0"
                        class="ml-1 mxs-color-helper text-grayed-out"
                    >
                        ({{ titleInfo }})
                    </span>
                </p>
                <v-fade-transition>
                    <v-btn v-if="showEditBtn || isEditing" icon class="edit-btn" @click="onEdit">
                        <v-icon color="primary" size="18">
                            $vuetify.icons.mxs_edit
                        </v-icon>
                    </v-btn>
                </v-fade-transition>
                <slot name="title-append" />
            </div>
            <v-spacer />
            <v-fade-transition>
                <v-btn
                    v-if="isEditing"
                    color="primary"
                    rounded
                    small
                    class="done-editing-btn text-capitalize"
                    @click="doneEditingCb"
                >
                    {{ $mxs_t('doneEditing') }}
                </v-btn>
            </v-fade-transition>
            <slot name="header-right" />
        </div>
        <v-expand-transition>
            <div v-show="isContentVisible" class="mxs-collapse-content">
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    /* SLOTS available for mxs-collapse */
    // name="content"
    props: {
        wrapperClass: { type: String, default: 'mxs-collapse-wrapper' },
        titleWrapperClass: String,
        // props for the toggle
        toggleOnClick: { type: Function, required: true },
        isContentVisible: { type: Boolean, required: true },
        // props for the Title
        title: { type: String, required: true },
        titleInfo: [String, Number], // option
        // edit button feat (peer required props)
        editable: { type: Boolean, default: false },
        onEdit: Function, // if this props is added, adding mouseenter event to handle show edit btn
        isEditing: Boolean, // show done editing btn and keep edit btn visible
        doneEditingCb: Function, // call back function triggering when click done editing btn
    },
    data() {
        return {
            showEditBtn: false,
        }
    },
}
</script>
