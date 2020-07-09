<template>
    <div
        :class="wrapperClass"
        v-on="
            $help.isFunction(onEdit)
                ? {
                      mouseenter: e => (showEditBtn = true),
                      mouseleave: e => (showEditBtn = false),
                  }
                : null
        "
    >
        <div class="mb-1 d-flex align-center">
            <div class="d-flex align-center" :class="titleWrapperClass">
                <v-btn icon class="arrow-toggle" @click="toggleOnClick">
                    <v-icon
                        :class="[!isContentVisible ? 'arrow-down' : 'arrow-up']"
                        size="32"
                        color="#013646"
                    >
                        $expand
                    </v-icon>
                </v-btn>
                <p class="mb-0 body-2 font-weight-bold color text-navigation text-uppercase">
                    {{ title }}
                    <span v-if="titleInfo || titleInfo === 0" class="ml-1 color text-field-text">
                        ({{ titleInfo }})
                    </span>
                </p>
                <v-fade-transition>
                    <v-btn
                        v-if="showEditBtn || isEditing"
                        icon
                        class="arrow-toggle"
                        @click="onEdit"
                    >
                        <v-icon color="primary" size="18">
                            $vuetify.icons.edit
                        </v-icon>
                    </v-btn>
                </v-fade-transition>
            </div>
            <v-spacer />
            <v-fade-transition>
                <v-btn
                    v-if="isEditing"
                    color="primary"
                    rounded
                    small
                    class="don-editing-btn text-capitalize"
                    @click="doneEditingCb"
                >
                    {{ $t('doneEditing') }}
                </v-btn>
            </v-fade-transition>

            <v-btn
                v-if="onAddClick"
                color="primary"
                text
                x-small
                class="add-btn text-capitalize"
                @click="onAddClick"
            >
                + {{ addBtnText }}
            </v-btn>
        </div>
        <v-expand-transition>
            <div v-show="isContentVisible">
                <slot name="content"></slot>
            </div>
        </v-expand-transition>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    /* SLOTS available for collapse */
    // name="content"
    props: {
        wrapperClass: String,
        titleWrapperClass: String,
        // props for the toggle
        toggleOnClick: { type: Function, required: true },
        isContentVisible: { type: Boolean, required: true },
        // props for the Title
        title: { type: String, required: true },
        titleInfo: [String, Number], // option
        // optional props for the + Add ... button ( peer required props)
        onAddClick: Function,
        addBtnText: { type: String, default: '+ Add' },
        // edit button feat (peer required props)
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
<style lang="scss">
.arrow-toggle {
    .arrow-up {
        transform: rotate(-180deg);
    }
}
</style>
